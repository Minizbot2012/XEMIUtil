#pragma once
#include <Forms.h>
#include <vector>
namespace MPL::Config
{
    struct ConfigEntry
    {
        std::unordered_set<Form> forms;
        Form xemi;
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
            if (!this->config_loaded)
            {
                if (std::filesystem::exists(this->config_path))
                {
                    for (auto file : std::filesystem::directory_iterator(this->config_path))
                    {
                        if (file.path().extension() == ".json")
                        {
                            auto cfg = rfl::json::load<std::vector<ConfigEntry>>(file.path().string());
                            if (cfg)
                            {
                                for (auto& conf : *cfg)
                                {
                                    this->entries.push_back(conf);
                                }
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
        }
        std::vector<ConfigEntry> entries;
    };
}  // namespace MPL::Config
