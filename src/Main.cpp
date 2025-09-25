#include <Config.h>
#include <Hooks.h>
#if defined(SKYRIM_AE) || defined(SKYRIM_SE)
extern "C" DLLEXPORT auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({ 1, 0, 0, 0 });
    v.PluginName("XEMIUtil");
    v.AuthorName("mini");
    v.UsesAddressLibrary();
    v.UsesNoStructs();
    return v;
}();

#elif defined(SKYRIM_VR)
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = "XEMIUtil";
    a_info->version = 0x10000000;
    if (a_skse->IsEditor())
    {
        return false;
    }
    const auto ver = a_skse->RuntimeVersion();
    if (ver < SKSE::RUNTIME_VR_1_4_15)
    {
        return false;
    }
    return true;
};

#endif
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    logger::info("Game version : {}", a_skse->RuntimeVersion().string());
    MPL::Hooks::Install();
    return true;
}