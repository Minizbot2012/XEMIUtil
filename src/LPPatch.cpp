#include <LPPatch.h>

#include <FormResolver.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace MPL::LPPatch
{
    namespace
    {
        using FormSet = std::unordered_set<RE::FormID>;
        using PlacementList = std::vector<const SourcePlacement*>;
        using Json = nlohmann::json;

        constexpr std::string_view kLogPrefix = "[Window Sync] Light Placer";
        const std::filesystem::path kConfigDirectory = R"(Data\LightPlacer)";
        constexpr std::size_t kMaxTransformerIDLength = 128;
        constexpr std::size_t kMaxTransformedJsonSize =
            64ULL * 1024ULL * 1024ULL;

        struct RegisteredTransformer
        {
            std::string id;
            XEMIAPI::LightPlacerTransform transform = nullptr;
            void (*onReloadComplete)() = nullptr;
        };

        std::mutex brokerLock;
        std::vector<PatchRule> retainedRules;
        std::vector<RegisteredTransformer> transformers;
        bool startupRulesReady = false;
        bool deferredReload = false;
        std::atomic<std::uint64_t> reloadGeneration{ 0 };

        struct PatchStats
        {
            std::size_t filesScanned = 0;
            std::size_t filesPatched = 0;
            std::size_t filesTransformed = 0;
            std::size_t lightEntriesPatched = 0;
            std::size_t lightEntriesSkippedForMalformedFilters = 0;
            std::size_t referencesPartitioned = 0;
            std::size_t transformerFailures = 0;
        };

        bool IEquals(const std::string_view a_lhs, const std::string_view a_rhs)
        {
            return a_lhs.size() == a_rhs.size() &&
                   _strnicmp(a_lhs.data(), a_rhs.data(), a_lhs.size()) == 0;
        }

        bool IsJsonFile(const std::filesystem::path& a_path)
        {
            return IEquals(a_path.extension().string(), ".json");
        }

        bool LightMatches(
            const std::string_view a_light,
            const std::unordered_set<std::string>& a_lights)
        {
            return std::ranges::any_of(a_lights, [&](const std::string& a_candidate) {
                return IEquals(a_light, a_candidate);
            });
        }

        std::string DescribeArray(
            const Json& a_object,
            const std::string_view a_key)
        {
            const auto found = a_object.find(a_key);
            return found != a_object.end() ? found->dump() : "<none>";
        }

        std::string DescribeForms(const FormSet& a_forms)
        {
            std::vector<std::string> forms;
            forms.reserve(a_forms.size());
            for (const auto form : a_forms)
            {
                forms.push_back(StableFormKey(form));
            }
            std::ranges::sort(forms);

            std::string result = "[";
            for (std::size_t index = 0; index < forms.size(); ++index)
            {
                if (index > 0)
                {
                    result += ", ";
                }
                result += forms[index];
            }
            result += "]";
            return result;
        }

        std::string DescribePlacements(const PlacementList& a_placements)
        {
            std::vector<std::string> placements;
            placements.reserve(a_placements.size());
            for (const auto* placement : a_placements)
            {
                if (placement)
                {
                    placements.push_back(std::format(
                        "{}@{}",
                        StableFormKey(placement->reference),
                        StableFormKey(placement->cell)));
                }
            }
            std::ranges::sort(placements);

            std::string result = "[";
            for (std::size_t index = 0; index < placements.size(); ++index)
            {
                if (index > 0)
                {
                    result += ", ";
                }
                result += placements[index];
            }
            result += "]";
            return result;
        }

        bool HasRelevantLight(
            const Json& a_source,
            const std::vector<PatchRule>& a_rules)
        {
            const auto lights = a_source.find("lights");
            if (lights == a_source.end() || !lights->is_array())
            {
                return false;
            }
            return std::ranges::any_of(*lights, [&](const Json& a_light) {
                const auto data =
                    a_light.is_object() ? a_light.find("data") : a_light.end();
                const auto light =
                    data != a_light.end() && data->is_object() ?
                        data->find("light") :
                        data->end();
                return light != data->end() && light->is_string() &&
                       std::ranges::any_of(a_rules, [&](const PatchRule& a_rule) {
                           return a_rule.detailedLogging &&
                                  LightMatches(
                                      light->get_ref<const std::string&>(),
                                      a_rule.lights);
                       });
            });
        }

        std::string ReadFile(const std::filesystem::path& a_path)
        {
            std::ifstream stream(a_path, std::ios::binary);
            return stream ?
                       std::string(
                           std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()) :
                       std::string{};
        }

        bool WriteFile(
            const std::filesystem::path& a_path,
            const std::string_view a_content)
        {
            std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                return false;
            }
            stream.write(
                a_content.data(),
                static_cast<std::streamsize>(a_content.size()));
            stream.flush();
            return static_cast<bool>(stream);
        }

        PlacementList PlacementsForSource(
            const Json& a_source,
            const PatchRule& a_rule)
        {
            FormSet sourceForms;
            if (const auto formIDs = a_source.find("formIDs");
                formIDs != a_source.end() && formIDs->is_array())
            {
                for (const auto& selector : *formIDs)
                {
                    if (selector.is_string())
                    {
                        const auto formID = FormResolver::Resolve(
                            selector.get_ref<const std::string&>());
                        if (formID)
                        {
                            sourceForms.insert(formID);
                        }
                    }
                }
            }

            std::unordered_set<std::string> sourceModels;
            if (const auto models = a_source.find("models");
                models != a_source.end() && models->is_array())
            {
                for (const auto& model : *models)
                {
                    if (model.is_string())
                    {
                        auto path = NormalizeModelPath(
                            model.get_ref<const std::string&>());
                        if (!path.empty())
                        {
                            sourceModels.insert(std::move(path));
                        }
                    }
                }
            }

            PlacementList placements;
            for (const auto& placement : a_rule.placements)
            {
                if (sourceForms.contains(placement.base) ||
                    sourceModels.contains(placement.model))
                {
                    placements.push_back(std::addressof(placement));
                }
            }
            return placements;
        }

        bool HasMalformedFilters(const Json& a_light)
        {
            for (const auto key : { "whiteList", "blackList" })
            {
                const auto found = a_light.find(key);
                if (found != a_light.end() && !found->is_array())
                {
                    return true;
                }
            }
            return false;
        }

        FormSet ResolveFilters(
            const Json& a_light,
            const std::string_view a_key)
        {
            FormSet filters;
            if (const auto list = a_light.find(a_key);
                list != a_light.end())
            {
                for (const auto& selector : *list)
                {
                    if (selector.is_string())
                    {
                        const auto form = FormResolver::Resolve(
                            selector.get_ref<const std::string&>());
                        if (form)
                        {
                            filters.insert(form);
                        }
                    }
                }
            }
            return filters;
        }

        PlacementList ApplyExistingFilters(
            const Json& a_light,
            const PlacementList& a_placements)
        {
            const bool hasWhiteList = a_light.contains("whiteList");
            const auto whiteList = ResolveFilters(a_light, "whiteList");
            const auto blackList = ResolveFilters(a_light, "blackList");

            PlacementList placements;
            for (const auto* placement : a_placements)
            {
                if (!placement)
                {
                    continue;
                }
                const auto matches = [&](const FormSet& a_filters) {
                    return std::ranges::any_of(
                        placement->filterIDs,
                        [&](const RE::FormID a_form) {
                            return a_filters.contains(a_form);
                        });
                };
                if (!matches(blackList) &&
                    (!hasWhiteList || matches(whiteList)))
                {
                    placements.push_back(placement);
                }
            }
            return placements;
        }

        FormSet PlacementReferences(const PlacementList& a_placements)
        {
            FormSet references;
            for (const auto* placement : a_placements)
            {
                if (placement)
                {
                    references.insert(placement->reference);
                }
            }
            return references;
        }

        void AppendFormKeys(Json& a_array, const FormSet& a_forms)
        {
            if (!a_array.is_array())
            {
                a_array = Json::array();
            }
            std::unordered_set<std::string> existing;
            for (const auto& value : a_array)
            {
                if (value.is_string())
                {
                    auto key = value.get<std::string>();
                    std::ranges::transform(key, key.begin(), [](const unsigned char a_character) {
                        return static_cast<char>(std::tolower(a_character));
                    });
                    existing.insert(std::move(key));
                }
            }
            std::vector<std::string> additions;
            additions.reserve(a_forms.size());
            for (const auto form : a_forms)
            {
                auto key = StableFormKey(form);
                auto normalized = key;
                std::ranges::transform(
                    normalized,
                    normalized.begin(),
                    [](const unsigned char a_character) {
                        return static_cast<char>(std::tolower(a_character));
                    });
                if (!key.empty() && existing.insert(std::move(normalized)).second)
                {
                    additions.push_back(std::move(key));
                }
            }
            std::ranges::sort(additions);
            for (auto& key : additions)
            {
                a_array.push_back(std::move(key));
            }
        }

        bool SameEmittance(
            const std::string_view a_existing,
            const std::string_view a_target)
        {
            if (IEquals(a_existing, a_target))
            {
                return true;
            }
            const auto existing = FormResolver::Resolve(a_existing);
            const auto target = FormResolver::Resolve(a_target);
            return existing && target && existing == target;
        }

        bool PatchSource(
            Json& a_source,
            const std::vector<PatchRule>& a_rules,
            PatchStats& a_stats,
            const std::filesystem::path& a_path)
        {
            auto lights = a_source.find("lights");
            if (lights == a_source.end() || !lights->is_array())
            {
                return false;
            }

            std::vector<PlacementList> sourcePlacements;
            sourcePlacements.reserve(a_rules.size());
            for (const auto& rule : a_rules)
            {
                sourcePlacements.push_back(
                    PlacementsForSource(a_source, rule));
            }
            if (std::ranges::none_of(
                    sourcePlacements,
                    [](const PlacementList& a_placements) {
                        return !a_placements.empty();
                }))
            {
                if (HasRelevantLight(a_source, a_rules))
                {
                    logger::info(
                        "{} decision: file='{}', source formIDs={}, models={}, "
                        "matching lpLight found but no source placement in a "
                        "classified cell matched",
                        kLogPrefix,
                        a_path.string(),
                        DescribeArray(a_source, "formIDs"),
                        DescribeArray(a_source, "models"));
                }
                return false;
            }

            bool changed = false;
            Json output = Json::array();
            for (const auto& inputLight : *lights)
            {
                if (!inputLight.is_object())
                {
                    output.push_back(inputLight);
                    continue;
                }
                const auto data = inputLight.find("data");
                if (data == inputLight.end() || !data->is_object())
                {
                    output.push_back(inputLight);
                    continue;
                }
                const auto light = data->find("light");
                const auto external = data->find("externalEmittance");
                if (light == data->end() || !light->is_string() ||
                    external == data->end() || !external->is_string() ||
                    external->get_ref<const std::string&>().empty())
                {
                    output.push_back(inputLight);
                    continue;
                }

                bool relevant = false;
                for (std::size_t index = 0; index < a_rules.size(); ++index)
                {
                    relevant |= !sourcePlacements[index].empty() &&
                                LightMatches(
                                    light->get_ref<const std::string&>(),
                                    a_rules[index].lights);
                }
                if (!relevant)
                {
                    output.push_back(inputLight);
                    continue;
                }
                if (HasMalformedFilters(inputLight))
                {
                    ++a_stats.lightEntriesSkippedForMalformedFilters;
                    output.push_back(inputLight);
                    continue;
                }

                Json original = inputLight;
                std::vector<Json> partitions;
                FormSet claimed;
                for (std::size_t offset = 0; offset < a_rules.size(); ++offset)
                {
                    const auto index = a_rules.size() - 1 - offset;
                    const auto& rule = a_rules[index];
                    if (!LightMatches(
                            light->get_ref<const std::string&>(),
                            rule.lights))
                    {
                        continue;
                    }

                    const auto& candidates = sourcePlacements[index];
                    const auto effective =
                        ApplyExistingFilters(inputLight, candidates);
                    auto references = PlacementReferences(effective);
                    if (rule.detailedLogging)
                    {
                        const auto points = inputLight.find("points");
                        logger::info(
                            "{} decision: file='{}', source formIDs={}, models={}, "
                            "light='{}', points={}, original emittance='{}', "
                            "whiteList={}, blackList={}, candidate references={}, "
                            "effective references={}, target emittance='{}'",
                            kLogPrefix,
                            a_path.string(),
                            DescribeArray(a_source, "formIDs"),
                            DescribeArray(a_source, "models"),
                            light->get_ref<const std::string&>(),
                            points != inputLight.end() && points->is_array() ?
                                points->size() :
                                0,
                            external->get_ref<const std::string&>(),
                            DescribeArray(inputLight, "whiteList"),
                            DescribeArray(inputLight, "blackList"),
                            DescribePlacements(candidates),
                            DescribePlacements(effective),
                            rule.externalEmittance);
                    }
                    std::erase_if(references, [&](const RE::FormID a_reference) {
                        return claimed.contains(a_reference);
                    });
                    if (references.empty())
                    {
                        continue;
                    }
                    claimed.insert(references.begin(), references.end());
                    if (SameEmittance(
                            external->get_ref<const std::string&>(),
                            rule.externalEmittance))
                    {
                        continue;
                    }

                    Json partition = inputLight;
                    partition["whiteList"] = Json::array();
                    AppendFormKeys(partition["whiteList"], references);
                    partition["data"]["externalEmittance"] =
                        rule.externalEmittance;
                    AppendFormKeys(original["blackList"], references);
                    partitions.push_back(std::move(partition));
                    a_stats.referencesPartitioned += references.size();
                }

                if (partitions.empty())
                {
                    output.push_back(inputLight);
                    continue;
                }
                changed = true;
                ++a_stats.lightEntriesPatched;
                output.push_back(std::move(original));
                for (auto& partition : partitions)
                {
                    output.push_back(std::move(partition));
                }
            }
            if (changed)
            {
                *lights = std::move(output);
            }
            return changed;
        }

        struct TransformOutput
        {
            std::string value;
            bool written = false;
            bool invalid = false;
        };

        bool StoreTransformOutput(
            void* a_context,
            const char* a_data,
            const std::size_t a_size)
        {
            auto* output = static_cast<TransformOutput*>(a_context);
            if (!output)
            {
                return false;
            }
            if (output->written || (!a_data && a_size != 0) ||
                a_size > kMaxTransformedJsonSize)
            {
                output->invalid = true;
                return false;
            }
            try
            {
                output->value.assign(
                    a_data ? a_data : "",
                    a_size);
                output->written = true;
                return true;
            }
            catch (...)
            {
                output->invalid = true;
                return false;
            }
        }

        bool ApplyTransformers(
            const std::filesystem::path& a_path,
            std::string& a_document,
            const std::vector<RegisteredTransformer>& a_transformers,
            PatchStats& a_stats)
        {
            bool changed = false;
            const auto path = a_path.string();
            for (const auto& transformer : a_transformers)
            {
                TransformOutput output;
                bool transformed = false;
                try
                {
                    transformed = transformer.transform(
                        path.c_str(),
                        a_document.data(),
                        a_document.size(),
                        StoreTransformOutput,
                        std::addressof(output));
                }
                catch (const std::exception& error)
                {
                    logger::error(
                        "{} transformer '{}' raised an exception for '{}': {}",
                        kLogPrefix,
                        transformer.id,
                        path,
                        error.what());
                }
                catch (...)
                {
                    logger::error(
                        "{} transformer '{}' raised an unknown exception for '{}'",
                        kLogPrefix,
                        transformer.id,
                        path);
                }
                if (!transformed || !output.written || output.invalid)
                {
                    ++a_stats.transformerFailures;
                    logger::error(
                        "{} transformer '{}' failed for '{}'; its changes were ignored",
                        kLogPrefix,
                        transformer.id,
                        path);
                    continue;
                }
                if (output.value == a_document)
                {
                    continue;
                }
                const auto validation =
                    Json::parse(output.value, nullptr, false);
                if (validation.is_discarded() || !validation.is_array())
                {
                    ++a_stats.transformerFailures;
                    logger::error(
                        "{} transformer '{}' returned invalid Light Placer JSON for '{}'; its changes were ignored",
                        kLogPrefix,
                        transformer.id,
                        path);
                    continue;
                }
                a_document = std::move(output.value);
                changed = true;
            }
            if (changed)
            {
                ++a_stats.filesTransformed;
            }
            return changed;
        }

        void EditConfigs(
            const std::vector<PatchRule>& a_rules,
            const std::vector<RegisteredTransformer>& a_transformers,
            PatchStats& a_stats,
            std::unordered_map<std::string, std::string>& a_backups)
        {
            std::error_code error;
            if (!std::filesystem::exists(kConfigDirectory, error))
            {
                logger::info(
                    "{} skipped because Data\\LightPlacer is not installed",
                    kLogPrefix);
                return;
            }

            for (std::filesystem::recursive_directory_iterator iterator(
                     kConfigDirectory,
                     std::filesystem::directory_options::skip_permission_denied,
                     error),
                 end;
                 iterator != end && !error;
                 iterator.increment(error))
            {
                const auto& entry = *iterator;
                if (!entry.is_regular_file(error) ||
                    !IsJsonFile(entry.path()))
                {
                    continue;
                }
                ++a_stats.filesScanned;
                const auto original = ReadFile(entry.path());
                if (original.empty())
                {
                    continue;
                }

                std::string transformed = original;
                bool changed = false;
                if (!a_rules.empty() &&
                    original.find("externalEmittance") != std::string::npos)
                {
                    Json document = Json::parse(original, nullptr, false);
                    if (document.is_discarded() || !document.is_array())
                    {
                        logger::warn(
                            "{} could not parse '{}'; leaving its XEMI partition unchanged",
                            kLogPrefix,
                            entry.path().string());
                    }
                    else
                    {
                        for (auto& source : document)
                        {
                            if (source.is_object())
                            {
                                changed |= PatchSource(
                                    source,
                                    a_rules,
                                    a_stats,
                                    entry.path());
                            }
                        }
                        if (changed)
                        {
                            transformed = document.dump(4);
                        }
                    }
                }

                changed |= ApplyTransformers(
                    entry.path(),
                    transformed,
                    a_transformers,
                    a_stats);
                if (!changed)
                {
                    continue;
                }

                const auto path = entry.path().string();
                if (WriteFile(entry.path(), transformed))
                {
                    a_backups.emplace(path, original);
                    ++a_stats.filesPatched;
                }
                else
                {
                    logger::error(
                        "{} could not write '{}'",
                        kLogPrefix,
                        path);
                    if (!WriteFile(entry.path(), original))
                    {
                        logger::critical(
                            "{} could not restore '{}' after the temporary write failed",
                            kLogPrefix,
                            path);
                    }
                }
            }
            if (error)
            {
                logger::error(
                    "{} stopped enumerating Data\\LightPlacer: {}",
                    kLogPrefix,
                    error.message());
            }
        }

        void RestoreConfigs(
            const std::unordered_map<std::string, std::string>& a_backups)
        {
            for (const auto& [path, original] : a_backups)
            {
                if (!WriteFile(path, original))
                {
                    logger::error(
                        "{} could not restore '{}'",
                        kLogPrefix,
                        path);
                }
            }
        }

        bool RunConsoleCommand(const std::string_view a_command)
        {
            auto* script = RE::IFormFactory::Create<RE::Script>();
            if (!script)
            {
                return false;
            }
            script->SetCommand(a_command);
            script->CompileAndRun(nullptr);
            delete script;
            return true;
        }

        void NotifyReloadComplete(
            const std::vector<RegisteredTransformer>& a_transformers)
        {
            for (const auto& transformer : a_transformers)
            {
                if (transformer.onReloadComplete)
                {
                    try
                    {
                        transformer.onReloadComplete();
                    }
                    catch (const std::exception& error)
                    {
                        logger::error(
                            "{} transformer '{}' raised an exception during reload completion: {}",
                            kLogPrefix,
                            transformer.id,
                            error.what());
                    }
                    catch (...)
                    {
                        logger::error(
                            "{} transformer '{}' raised an unknown exception during reload completion",
                            kLogPrefix,
                            transformer.id);
                    }
                }
            }
        }

        void ApplyAndReload(
            const std::vector<PatchRule>& a_rules,
            const std::vector<RegisteredTransformer>& a_transformers)
        {
            PatchStats stats;
            std::unordered_map<std::string, std::string> backups;
            try
            {
                EditConfigs(
                    a_rules,
                    a_transformers,
                    stats,
                    backups);
                if (backups.empty())
                {
                    logger::info(
                        "{} scanned {} file(s); no eligible definitions required partitioning or tuning",
                        kLogPrefix,
                        stats.filesScanned);
                    NotifyReloadComplete(a_transformers);
                    return;
                }

                const bool reloaded =
                    RunConsoleCommand("ReloadLP");
                RestoreConfigs(backups);
                backups.clear();
                logger::info(
                    "{} combined patch: files={}/{}, transformed files={}, definitions={}, "
                    "reference targets={}, malformed filtered definitions skipped={}, "
                    "transformer failures={}, ReloadLP={}, originals restored",
                    kLogPrefix,
                    stats.filesPatched,
                    stats.filesScanned,
                    stats.filesTransformed,
                    stats.lightEntriesPatched,
                    stats.referencesPartitioned,
                    stats.lightEntriesSkippedForMalformedFilters,
                    stats.transformerFailures,
                    reloaded);
            }
            catch (const std::exception& error)
            {
                RestoreConfigs(backups);
                logger::error(
                    "{} broker operation failed and restored {} temporary file(s): {}",
                    kLogPrefix,
                    backups.size(),
                    error.what());
            }
            catch (...)
            {
                RestoreConfigs(backups);
                logger::error(
                    "{} broker operation failed with an unknown exception and restored {} temporary file(s)",
                    kLogPrefix,
                    backups.size());
            }
            NotifyReloadComplete(a_transformers);
        }

        void QueueReload()
        {
            const auto generation = ++reloadGeneration;
            if (const auto* tasks = SKSE::GetTaskInterface())
            {
                tasks->AddTask([generation]() {
                    if (generation !=
                        reloadGeneration.load(std::memory_order_acquire))
                    {
                        return;
                    }
                    std::vector<PatchRule> rules;
                    std::vector<RegisteredTransformer> callbacks;
                    {
                        std::scoped_lock lock(brokerLock);
                        rules = retainedRules;
                        callbacks = transformers;
                    }
                    ApplyAndReload(rules, callbacks);
                });
            }
            else
            {
                logger::error(
                    "{} could not queue because the SKSE task interface is unavailable",
                    kLogPrefix);
            }
        }
    }  // namespace

    std::string NormalizeModelPath(std::string_view a_path)
    {
        std::string result(a_path);
        std::ranges::replace(result, '/', '\\');
        std::ranges::transform(
            result,
            result.begin(),
            [](const unsigned char a_character) {
                return static_cast<char>(std::tolower(a_character));
            });
        constexpr std::array prefixes{
            std::string_view{ "data\\meshes\\" },
            std::string_view{ "meshes\\" },
        };
        for (const auto prefix : prefixes)
        {
            if (result.starts_with(prefix))
            {
                result.erase(0, prefix.size());
                break;
            }
        }
        return result;
    }

    std::string StableFormKey(const RE::FormID a_formID)
    {
        if (!a_formID)
        {
            return {};
        }

        auto* dataHandler =
            RE::TESDataHandler::GetSingleton();
        if (!dataHandler)
        {
            return std::format("0x{:X}", a_formID);
        }

        const bool isLightForm =
            (a_formID & 0xFF000000) == 0xFE000000;
        const auto fullIndex =
            static_cast<std::uint8_t>(a_formID >> 24);
        if (!isLightForm && fullIndex == 0xFF)
        {
            return std::format("0x{:X}", a_formID);
        }
        const auto* file =
            isLightForm ?
                dataHandler->LookupLoadedLightModByIndex(
                    static_cast<std::uint16_t>(
                        (a_formID & 0x00FFF000) >> 12)) :
                dataHandler->LookupLoadedModByIndex(
                    fullIndex);
        if (!file)
        {
            return std::format("0x{:X}", a_formID);
        }

        const RE::FormID localFormID =
            isLightForm ?
                a_formID & 0x00000FFF :
                a_formID & 0x00FFFFFF;
        return std::format(
            "0x{:X}~{}",
            localFormID,
            file->GetFilename());
    }

    void QueueStartupPatch(std::vector<PatchRule> a_rules)
    {
        std::erase_if(a_rules, [](const PatchRule& a_rule) {
            return a_rule.lights.empty() ||
                   a_rule.externalEmittance.empty() ||
                   a_rule.placements.empty();
        });
        bool queue = false;
        std::size_t ruleCount = 0;
        {
            std::scoped_lock lock(brokerLock);
            retainedRules = std::move(a_rules);
            startupRulesReady = true;
            queue = !retainedRules.empty() || deferredReload;
            ruleCount = retainedRules.size();
            deferredReload = false;
        }
        if (!queue)
        {
            return;
        }
        logger::info(
            "{} queued a combined reload with {} selective rule(s)",
            kLogPrefix,
            ruleCount);
        QueueReload();
    }

    bool RegisterTransformer(
        const XEMIAPI::LightPlacerTransformer* a_transformer)
    {
        if (!a_transformer)
        {
            return false;
        }
        const auto idLength =
            a_transformer->id ?
                strnlen_s(
                    a_transformer->id,
                    kMaxTransformerIDLength + 1) :
                0;
        if (idLength == 0 ||
            idLength > kMaxTransformerIDLength ||
            !a_transformer->TransformJson)
        {
            return false;
        }
        RegisteredTransformer value;
        try
        {
            value = {
                .id = a_transformer->id,
                .transform = a_transformer->TransformJson,
                .onReloadComplete =
                    a_transformer->OnReloadComplete,
            };
            std::scoped_lock lock(brokerLock);
            const auto existing = std::ranges::find_if(
                transformers,
                [&](const RegisteredTransformer& a_registered) {
                    return IEquals(
                        a_registered.id,
                        a_transformer->id);
                });
            if (existing != transformers.end())
            {
                if (existing->transform == value.transform &&
                    existing->onReloadComplete ==
                        value.onReloadComplete)
                {
                    return true;
                }
                logger::error(
                    "{} rejected duplicate transformer ID '{}'",
                    kLogPrefix,
                    value.id);
                return false;
            }
            transformers.push_back(value);
        }
        catch (const std::exception& error)
        {
            logger::error(
                "{} could not register transformer '{}': {}",
                kLogPrefix,
                a_transformer->id,
                error.what());
            return false;
        }
        catch (...)
        {
            logger::error(
                "{} could not register transformer '{}' because an unknown exception occurred",
                kLogPrefix,
                a_transformer->id);
            return false;
        }
        logger::info(
            "{} registered transformer '{}'",
            kLogPrefix,
            value.id);
        return true;
    }

    bool RequestReload()
    {
        try
        {
            if (!SKSE::GetTaskInterface())
            {
                return false;
            }

            {
                std::scoped_lock lock(brokerLock);
                if (!startupRulesReady)
                {
                    deferredReload = true;
                    return true;
                }
            }
            QueueReload();
            return true;
        }
        catch (const std::exception& error)
        {
            logger::error(
                "{} reload request failed: {}",
                kLogPrefix,
                error.what());
        }
        catch (...)
        {
            logger::error(
                "{} reload request failed with an unknown exception",
                kLogPrefix);
        }
        return false;
    }
}  // namespace MPL::LPPatch
