#include <Config.h>
#include <Hooks.h>
#include <Plugin.h>
#ifdef SKYRIM_AE
extern "C" DLLEXPORT auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion(MPL::Plugin::MAJOR);
    v.PluginName(MPL::Plugin::PROJECT.data());
    v.AuthorName("Mini");
    v.UsesAddressLibrary();
    v.UsesNoStructs();
    return v;
}();
#else

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = MPL::Plugin::PROJECT.data();
    a_info->version = MPL::Plugin::MAJOR;
    if (a_skse->IsEditor())
    {
        return false;
    }
    auto ver = a_skse->RuntimeVersion();
    if(ver > SKSE::RUNTIME_SSE_1_5_97) {
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