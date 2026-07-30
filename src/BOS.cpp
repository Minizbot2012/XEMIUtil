#include <BOS.h>
#include <FormResolver.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace MPL::BOS
{
    namespace
    {
        using Json = nlohmann::json;
        using SwapMap =
            std::unordered_map<RE::FormID, RE::FormID>;

        constexpr std::string_view kLogPrefix =
            "[Window Sync] Direct BOS";
        const std::filesystem::path kDataDirectory = "Data";
        const std::filesystem::path kConfiguration =
            R"(Data\Luma\WeatherSync\Helios.json)";

        enum class Section
        {
            kNone,
            kForms,
            kReferences,
        };

        struct Rules
        {
            SwapMap forms;
            SwapMap references;
        };

        std::string_view Trim(std::string_view a_value)
        {
            while (!a_value.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(a_value.front())))
            {
                a_value.remove_prefix(1);
            }
            while (!a_value.empty() &&
                   std::isspace(
                       static_cast<unsigned char>(a_value.back())))
            {
                a_value.remove_suffix(1);
            }
            return a_value;
        }

        std::vector<std::string> LoadFiles()
        {
            std::ifstream stream(kConfiguration, std::ios::binary);
            if (!stream)
            {
                logger::warn(
                    "{} could not read '{}'",
                    kLogPrefix,
                    kConfiguration.string());
                return {};
            }

            const auto root =
                Json::parse(stream, nullptr, false, true);
            if (root.is_discarded() || !root.is_object())
            {
                logger::warn(
                    "{} could not parse '{}'",
                    kLogPrefix,
                    kConfiguration.string());
                return {};
            }

            const auto windowSync = root.find("windowSync");
            if (windowSync == root.end() ||
                !windowSync->is_object())
            {
                return {};
            }
            const auto configured =
                windowSync->find("bosSwapFiles");
            if (configured == windowSync->end() ||
                !configured->is_array())
            {
                return {};
            }

            std::vector<std::string> files;
            for (const auto& file : *configured)
            {
                if (file.is_string())
                {
                    files.push_back(file.get<std::string>());
                }
            }
            std::ranges::sort(files);
            return files;
        }

        Section ParseSection(const std::string_view a_line)
        {
            if (a_line == "[Forms]")
            {
                return Section::kForms;
            }
            if (a_line == "[References]")
            {
                return Section::kReferences;
            }
            return Section::kNone;
        }

        bool ParseFile(
            const std::filesystem::path& a_path,
            Rules& a_rules)
        {
            std::ifstream stream(a_path, std::ios::binary);
            if (!stream)
            {
                logger::warn(
                    "{} could not read configured file '{}'",
                    kLogPrefix,
                    a_path.string());
                return false;
            }

            Section section = Section::kNone;
            std::string line;
            while (std::getline(stream, line))
            {
                const auto trimmed = Trim(line);
                if (trimmed.empty() || trimmed.front() == ';')
                {
                    continue;
                }
                if (trimmed.front() == '[')
                {
                    section = ParseSection(trimmed);
                    continue;
                }
                if (section == Section::kNone)
                {
                    continue;
                }

                const auto separator = trimmed.find('|');
                if (separator == std::string_view::npos ||
                    trimmed.find('|', separator + 1) !=
                        std::string_view::npos)
                {
                    continue;
                }

                const auto source =
                    Trim(trimmed.substr(0, separator));
                const auto target =
                    Trim(trimmed.substr(separator + 1));
                if (source.empty() || target.empty() ||
                    source.contains(',') || target.contains(','))
                {
                    continue;
                }

                const auto sourceID =
                    FormResolver::Resolve(source);
                const auto targetID =
                    FormResolver::Resolve(target);
                if (!sourceID || !targetID)
                {
                    continue;
                }

                auto& rules = section == Section::kForms ?
                                  a_rules.forms :
                                  a_rules.references;
                rules.insert_or_assign(sourceID, targetID);
            }
            return true;
        }
    }  // namespace

    void ApplyConfiguredSwaps(PluginIndex::Result& a_index)
    {
        const auto files = LoadFiles();
        Rules rules;
        std::size_t filesRead = 0;
        for (const auto& file : files)
        {
            filesRead +=
                ParseFile(kDataDirectory / file, rules) ? 1 : 0;
        }

        std::size_t changedPlacements = 0;
        for (auto& [referenceID, placement] :
            a_index.placements)
        {
            if (placement.deleted)
            {
                continue;
            }

            RE::FormID swappedBase = 0;
            if (const auto reference =
                    rules.references.find(referenceID);
                reference != rules.references.end())
            {
                swappedBase = reference->second;
            }
            else if (const auto form =
                         rules.forms.find(placement.base);
                form != rules.forms.end())
            {
                swappedBase = form->second;
            }
            if (swappedBase && swappedBase != placement.base)
            {
                placement.base = swappedBase;
                ++changedPlacements;
            }
        }

        logger::info(
            "{} completed: files={}/{}, form rules={}, "
            "reference rules={}, changed placements={}",
            kLogPrefix,
            filesRead,
            files.size(),
            rules.forms.size(),
            rules.references.size(),
            changedPlacements);
    }
}  // namespace MPL::BOS
