#pragma once
#include <Forms.h>
#include <unordered_set>
#include <vector>
namespace MPL::Config
{
    struct ConfigEntry
    {
        std::unordered_set<LiteForm> forms;
        LiteForm xemi;
        std::optional<std::unordered_set<LiteForm>> allowed_cells;
        std::optional<bool> remove;
        std::optional<bool> only_interior;
        std::optional<bool> forms_are_base;
    };
    class StatData : public REX::Singleton<StatData>
    {
    private:
        std::mutex load_lock;
        bool config_loaded = false;
        const static inline std::string config_path = "Data/SKSE/XEMIUtil";

    public:
        inline void LoadConfig()
        {
            if (this->config_loaded) return;
            std::lock_guard _guard(this->load_lock);
            logger::info("Begin loading config");
            if (!this->config_loaded)
            {
                if (std::filesystem::exists(this->config_path))
                {
                    for (auto file : std::filesystem::directory_iterator(this->config_path))
                    {
                        if (file.path().extension() == ".json")
                        {
                            if (auto cfg = rfl::json::load<std::vector<ConfigEntry>>(file.path().string()); cfg.has_value())
                            {
                                for (auto& conf : *cfg)
                                {
                                    this->entries.push_back(conf);
                                }
                            }
                            else if (auto cfg = rfl::json::load<ConfigEntry>(file.path().string()); cfg.has_value())
                            {
                                this->entries.push_back(*cfg);
                            }
                            else
                            {
                                logger::error("Error {}, skipping", cfg.error().what());
                            }
                        }
                    }
                    logger::info("Loaded {} Entries", this->entries.size());
                }
                else
                {
                    logger::error("Config path does not exist, skipping the loading of records.");
                }
                this->config_loaded = true;
            }
            logger::info("Finished loading config");
        }
        std::vector<ConfigEntry> entries;
    };
}  // namespace MPL::Config
