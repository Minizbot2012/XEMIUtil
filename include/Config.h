#pragma once
#include <MergeMapperPluginAPI.h>
namespace MPL::Config
{
    struct ConfigEntry
    {
        std::unordered_set<RE::FormID> forms;
        RE::FormID xemi;
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
            if (!this->config_loaded)
            {
                this->config_loaded = true;
                if (std::filesystem::exists(this->config_path))
                {
                    for (auto file : std::filesystem::directory_iterator(this->config_path))
                    {
                        if (file.path().extension() == ".json")
                        {
                            auto cfg = rfl::json::load<std::vector<ConfigEntry>>(file.path().string());
                            if (cfg)
                            {
                                ConfigEntry* inst = nullptr;
                                for (auto& conf : *cfg)
                                {
                                    for (ConfigEntry ent : this->entries)
                                    {
                                        if (conf.xemi == ent.xemi)
                                        {
                                            inst = &ent;
                                            break;
                                        }
                                    }
                                    if (inst == nullptr)
                                    {
                                        logger::info("Adding new XEMI Entry: {:8X}", conf.xemi);
                                        this->entries.push_back(conf);
                                    }
                                    else
                                    {
                                        logger::info("Found Merge XEMI {:8X}", inst->xemi);
                                        for (auto form : conf.forms)
                                        {
                                            inst->forms.insert(form);
                                        }
                                    }
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
            }
        }
        std::vector<ConfigEntry> entries;
    };
}  // namespace MPL::Config

namespace rfl
{
    static bool tried_mmi = false;
    template <>
    struct Reflector<RE::FormID>
    {
        using ReflType = std::string;
        static ReflType from(const RE::FormID& v)
        {
            auto frm = RE::TESForm::LookupByID(v);
            std::pair<const char*, uint32_t> ofid;
            if (!tried_mmi && !g_mergeMapperInterface)
            {
                MergeMapperPluginAPI::GetMergeMapperInterface001();
                tried_mmi = true;
            }
            if (g_mergeMapperInterface != nullptr && g_mergeMapperInterface->isMerge(frm->sourceFiles.array->front()->GetFilename().data()))
            {
                ofid = g_mergeMapperInterface->GetOriginalFormID(frm->sourceFiles.array->front()->GetFilename().data(), frm->GetLocalFormID());
            }
            else
            {
                ofid = std::make_pair(frm->sourceFiles.array->front()->GetFilename().data(), frm->GetLocalFormID());
            }
            return std::format("{:06X}:{}", ofid.second, ofid.second);
        }
        static RE::FormID to(const ReflType& v)
        {
            auto loc = v.find(":");
            if (loc != std::string::npos)
            {
                auto lfid = strtoul(v.substr(0, loc).c_str(), nullptr, 16);
                auto file = v.substr(loc + 1);
                if (!tried_mmi && !g_mergeMapperInterface)
                {
                    MergeMapperPluginAPI::GetMergeMapperInterface001();
                    tried_mmi = true;
                }
                if (g_mergeMapperInterface != nullptr && g_mergeMapperInterface->wasMerged(file.c_str()))
                {
                    auto nd = g_mergeMapperInterface->GetNewFormID(file.c_str(), lfid);
                    file = std::string(nd.first);
                    lfid = nd.second;
                }
                auto dh = RE::TESDataHandler::GetSingleton();
                return dh->LookupFormID(lfid, file);
            }
            else
            {
                auto frm = RE::TESForm::LookupByEditorID(v);
                if (frm)
                {
                    return frm->GetFormID();
                }
                else
                {
                    return RE::FormID(0);
                }
            }
        }
    };
}  // namespace rfl
