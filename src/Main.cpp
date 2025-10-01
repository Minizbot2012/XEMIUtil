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

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    auto ver = a_skse->RuntimeVersion();
    if(ver <= SKSE::RUNTIME_SSE_1_6_629) {
        stl::report_and_fail("This is the improper version for your game. This is setup so that this plugin doesn't silently fail on your version. Please use the SE version in the fomod.");
    }
    return false;
}
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
    const auto ver = a_skse->RuntimeVersion();
    #ifdef SKYRIMVR
    if (ver < SKSE::RUNTIME_VR_1_4_15)
    #else
    if(ver != SKSE::RUNTIME_SSE_1_5_97)
    #endif
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