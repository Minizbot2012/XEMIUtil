#pragma once
#include <Forms.h>
#include <unordered_set>
#include <vector>
namespace MPL::Config
{
    struct CellContainsSettings
    {
        std::unordered_set<LiteForm> forms;
        std::optional<std::string> profile;
        std::optional<std::vector<std::string>> profiles;
        std::optional<bool> forms_are_base;
        std::optional<bool> apply_xemi;
        // Deprecated compatibility field. Detailed logging is controlled by
        // SKSE\Plugins\XEMIUtilSettings.json.
        std::optional<bool> debug_logging;
    };

    struct ConfigEntry
    {
        std::unordered_set<LiteForm> forms;
        std::optional<std::unordered_set<std::string>> lpLight;
        LiteForm xemi;
        std::optional<std::unordered_set<LiteForm>> allowed_cells;
        std::optional<std::unordered_set<LiteForm>> excluded_cells;
        std::optional<bool> remove;
        std::optional<bool> only_interior;
        std::optional<bool> forms_are_base;
        std::optional<CellContainsSettings> cellContains;
        std::optional<CellContainsSettings> windows;
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
            std::lock_guard _guard(this->load_lock);
            if (this->config_loaded)
            {
                return;
            }
            logger::info("Begin loading config");

            std::error_code error;
            if (std::filesystem::is_directory(this->config_path, error))
            {
                for (std::filesystem::directory_iterator iterator(
                         this->config_path,
                         std::filesystem::directory_options::skip_permission_denied,
                         error),
                     end;
                     iterator != end && !error;
                     iterator.increment(error))
                {
                    std::error_code entryError;
                    if (!iterator->is_regular_file(entryError) ||
                        iterator->path().extension() != ".json")
                    {
                        continue;
                    }
                    const auto path = iterator->path().string();
                    if (auto cfg = rfl::json::load<std::vector<ConfigEntry>>(path);
                        cfg.has_value())
                    {
                        for (auto& conf : *cfg)
                        {
                            this->entries.push_back(std::move(conf));
                        }
                    }
                    else if (auto conf = rfl::json::load<ConfigEntry>(path);
                        conf.has_value())
                    {
                        this->entries.push_back(std::move(*conf));
                    }
                    else
                    {
                        logger::error(
                            "Could not read XEMI configuration '{}': {}",
                            path,
                            cfg.error().what());
                    }
                }
                if (error)
                {
                    logger::error(
                        "Stopped enumerating XEMI configurations: {}",
                        error.message());
                }
                logger::info("Loaded {} Entries", this->entries.size());
            }
            else
            {
                logger::error(
                    "Config path does not exist or is unavailable; skipping records (error={})",
                    error ? error.message() : "none");
            }
            this->config_loaded = true;
            logger::info("Finished loading config");
        }
        std::vector<ConfigEntry> entries;
    };
}  // namespace MPL::Config
