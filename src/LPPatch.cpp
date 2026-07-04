#include <LPPatch.h>
#include <atomic>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace MPL::LPPatch
{
    static bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
    {
        return a_lhs.size() == a_rhs.size() &&
               _strnicmp(a_lhs.data(), a_rhs.data(), a_lhs.size()) == 0;
    }

    static bool IsJsonFile(const std::filesystem::path& a_path)
    {
        return IEquals(a_path.extension().string(), ".json");
    }

    static void AddRule(std::vector<PatchRule>& a_rules, std::string a_light, std::string a_externalEmittance)
    {
        if (!a_light.empty() && !a_externalEmittance.empty())
        {
            a_rules.push_back({ std::move(a_light), std::move(a_externalEmittance) });
        }
    }

    static void AddRulesFromLightNode(std::vector<PatchRule>& a_rules, rfl::Generic& a_lightNode, const std::string& a_externalEmittance)
    {
        if (auto light = a_lightNode.to_string())
        {
            AddRule(a_rules, *light, a_externalEmittance);
            return;
        }

        auto& value = a_lightNode.get();
        if (auto* lights = std::get_if<rfl::Generic::Array>(&value))
        {
            for (auto& lightNode : *lights)
            {
                if (auto light = lightNode.to_string())
                {
                    AddRule(a_rules, *light, a_externalEmittance);
                }
            }
        }
    }

    static int AddRulesFromConfig(std::vector<PatchRule>& a_rules, rfl::Generic& a_config)
    {
        int added = 0;
        auto& value = a_config.get();
        auto* rules = std::get_if<rfl::Generic::Array>(&value);
        if (rules == nullptr)
        {
            return added;
        }

        for (auto& ruleNode : *rules)
        {
            auto& ruleValue = ruleNode.get();
            auto* obj = std::get_if<rfl::Generic::Object>(&ruleValue);
            if (obj == nullptr)
            {
                continue;
            }

            std::string   externalEmittance;
            rfl::Generic* lightNode = nullptr;

            for (auto& [key, child] : *obj)
            {
                if (key == "externalEmittance")
                {
                    if (auto externalEmittanceValue = child.to_string())
                    {
                        externalEmittance = *externalEmittanceValue;
                    }
                }
                else if (key == "light" || key == "lights")
                {
                    lightNode = &child;
                }
            }

            if (!externalEmittance.empty() && lightNode != nullptr)
            {
                const auto before = a_rules.size();
                AddRulesFromLightNode(a_rules, *lightNode, externalEmittance);
                added += static_cast<int>(a_rules.size() - before);
            }
        }

        return added;
    }

    static std::vector<PatchRule> LoadRules()
    {
        std::vector<PatchRule> rules;
        std::error_code        ec;
        if (!std::filesystem::exists(RULES_DIR, ec))
        {
            return rules;
        }
        for (const auto& file : std::filesystem::directory_iterator(RULES_DIR, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (!file.is_regular_file(ec) || !IsJsonFile(file.path()))
            {
                continue;
            }
            auto parsed = rfl::json::load<rfl::Generic>(file.path().string());
            if (!parsed)
            {
                logger::error("LPPatch: failed to read {} ({})", file.path().string(), parsed.error().what());
                continue;
            }
            const int added = AddRulesFromConfig(rules, *parsed);
            if (added == 0)
            {
                logger::warn("LPPatch: no valid rules found in {}", file.path().string());
            }
        }
        return rules;
    }

    static std::string ReadFile(const std::filesystem::path& a_path)
    {
        std::ifstream stream(a_path, std::ios::binary);
        if (!stream)
        {
            return {};
        }
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    static bool WriteFile(const std::filesystem::path& a_path, const std::string& a_content)
    {
        std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }
        stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
        return static_cast<bool>(stream);
    }

    // Recursively walks a parsed Light Placer config and rewrites the `externalEmittance` of
    // every light-data object whose `light` matches a rule. Returns the number of edits made.
    static int PatchNode(rfl::Generic& a_node, const std::vector<PatchRule>& a_rules)
    {
        int edits = 0;
        auto& value = a_node.get();
        if (auto* obj = std::get_if<rfl::Generic::Object>(&value))
        {
            // A Light Placer light-data object is the only object that carries a string "light".
            const PatchRule* match = nullptr;
            for (auto& [key, child] : *obj)
            {
                if (key == "light")
                {
                    if (auto light = child.to_string())
                    {
                        for (const auto& rule : a_rules)
                        {
                            if (IEquals(*light, rule.light))
                            {
                                match = &rule;
                                break;
                            }
                        }
                    }
                    break;
                }
            }
            if (match != nullptr)
            {
                // Only redirect an emittance that already exists; never add one to a bulb that
                // has none (a bulb without externalEmittance is intentionally not weather-driven).
                for (auto& [key, child] : *obj)
                {
                    if (key == "externalEmittance")
                    {
                        auto current = child.to_string();
                        if (current && !current->empty() && !IEquals(*current, match->externalEmittance))
                        {
                            child = rfl::Generic{ match->externalEmittance };
                            ++edits;
                        }
                        break;
                    }
                }
            }
            for (auto& [key, child] : *obj)
            {
                edits += PatchNode(child, a_rules);
            }
        }
        else if (auto* arr = std::get_if<rfl::Generic::Array>(&value))
        {
            for (auto& element : *arr)
            {
                edits += PatchNode(element, a_rules);
            }
        }
        return edits;
    }

    // Rewrites all matching Light Placer config files on disk, returning a map of path ->
    // original file contents so the edits can be reverted once Light Placer has re-read them.
    static std::unordered_map<std::string, std::string> EditConfigs(const std::vector<PatchRule>& a_rules)
    {
        std::unordered_map<std::string, std::string> backups;
        std::error_code                              ec;
        if (!std::filesystem::exists(LP_CONFIG_DIR, ec))
        {
            return backups;
        }
        int filesScanned = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(LP_CONFIG_DIR, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (!entry.is_regular_file(ec) || !IsJsonFile(entry.path()))
            {
                continue;
            }
            ++filesScanned;
            const auto  path = entry.path();
            std::string original = ReadFile(path);
            if (original.empty())
            {
                logger::warn("LPPatch: could not read (or empty) {}", path.string());
                continue;
            }
            // Cheap pre-filter: a file can only be edited if it contains an externalEmittance
            // key, so skip the full JSON parse for any file that has none.
            if (original.find("externalEmittance") == std::string::npos)
            {
                continue;
            }
            auto parsed = rfl::json::read<rfl::Generic>(original);
            if (!parsed)
            {
                logger::warn("LPPatch: could not parse {} ({})", path.string(), parsed.error().what());
                continue;
            }
            const int edits = PatchNode(*parsed, a_rules);
            if (edits == 0)
            {
                continue;
            }
            const std::string edited = rfl::json::write(*parsed);
            if (WriteFile(path, edited))
            {
                backups.emplace(path.string(), std::move(original));
            }
            else
            {
                logger::error("LPPatch: failed to write {}", path.string());
            }
        }
        logger::info("LPPatch: scanned {} Light Placer config file(s), patched {}", filesScanned, backups.size());
        return backups;
    }

    static void RestoreConfigs(const std::unordered_map<std::string, std::string>& a_backups)
    {
        for (const auto& [path, original] : a_backups)
        {
            if (!WriteFile(path, original))
            {
                logger::error("LPPatch: failed to restore {}", path);
            }
        }
    }

    class ConfigRestoreGuard
    {
    public:
        explicit ConfigRestoreGuard(std::unordered_map<std::string, std::string> a_backups) :
            backups(std::move(a_backups))
        {}

        ~ConfigRestoreGuard()
        {
            Restore();
        }

        void Restore()
        {
            if (!restored.exchange(true))
            {
                RestoreConfigs(backups);
            }
        }

    private:
        std::unordered_map<std::string, std::string> backups;
        std::atomic_bool                             restored{ false };
    };

    // Runs a console command (used to fire Light Placer's "ReloadLP"). The command executes
    // synchronously: by the time this returns, Light Placer has re-read the edited configs.
    static void RunConsoleCommand(std::string_view a_command)
    {
        auto* script = RE::IFormFactory::Create<RE::Script>();
        if (script == nullptr)
        {
            logger::error("LPPatch: could not create console script");
            return;
        }
        script->SetCommand(a_command);
        script->CompileAndRun(nullptr);
        delete script;
    }

    static void ApplyAndReload()
    {
        const auto rules = LoadRules();
        if (rules.empty())
        {
            return;
        }
        auto backups = EditConfigs(rules);
        if (backups.empty())
        {
            return;
        }
        ConfigRestoreGuard restoreGuard(std::move(backups));
        // ReloadLP: re-reads the edited configs now, and defers the bulb re-attach to a later
        // task that uses the (already loaded) in-memory data -- so restoring the files here is safe.
        RunConsoleCommand("ReloadLP");
    }

    // Debounced entry point. Multiple triggers can fire close together (the many
    // TESCellFullyLoadedEvents of a single load, plus kPostLoadGame right after), so collapse
    // anything within a few seconds of the previous apply into one.
    static std::atomic<std::uint64_t> g_lastApplyTick{ 0 };

    static void RequestApply()
    {
        const std::uint64_t now = GetTickCount64();
        std::uint64_t       prev = g_lastApplyTick.load();
        if (prev != 0 && now - prev < 3000)
        {
            return;
        }
        if (!g_lastApplyTick.compare_exchange_strong(prev, now))
        {
            return;
        }
        if (const auto* task = SKSE::GetTaskInterface())
        {
            task->AddTask([]() { ApplyAndReload(); });
        }
        else
        {
            logger::error("LPPatch: SKSE task interface unavailable, cannot schedule patch");
        }
    }

    // OnInit() equivalent: the first cell to finish loading covers a new game AND `coc` from the
    // main menu. Guarded so the many cell-load events of a single load only apply once; save
    // loads are handled separately by kPostLoadGame. This is a global ScriptEventSourceHolder
    // event, so - unlike PlayerCharacter's BGSActorCellEvent, whose base offset shifts between
    // game versions - it has a fixed layout and is safe to subscribe to from a cross-version build.
    class CellSink : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
    {
    public:
        static CellSink* GetSingleton()
        {
            static CellSink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
        {
            if (a_event != nullptr && !firstCellLoaded.exchange(true))
            {
                RequestApply();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        std::atomic_bool firstCellLoaded{ false };
    };

    static void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        switch (a_msg->type)
        {
        case SKSE::MessagingInterface::kDataLoaded:
            // OnInit(): register the first-cell-load trigger (new game / coc).
            if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton())
            {
                holder->GetEventSource<RE::TESCellFullyLoadedEvent>()->AddEventSink(CellSink::GetSingleton());
            }
            else
            {
                logger::error("LPPatch: ScriptEventSourceHolder unavailable, cannot register cell listener");
            }
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            // OnPlayerLoadGame(): re-apply whenever a save is loaded, even mid-session.
            RequestApply();
            break;
        case SKSE::MessagingInterface::kNewGame:
            // New games can happen more than once in a single process after returning to the
            // main menu, so do not rely only on the first cell event.
            RequestApply();
            break;
        default:
            break;
        }
    }

    void Install()
    {
        if (auto* messaging = SKSE::GetMessagingInterface())
        {
            messaging->RegisterListener(OnMessage);
        }
    }
}  // namespace MPL::LPPatch
