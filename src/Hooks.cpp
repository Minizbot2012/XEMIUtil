#include <Config.h>
#include <Hooks.h>
#include <WindowSync.h>
namespace MPL::Hooks
{
    struct InitItemImpl_TESObjectREFR
    {
        using Target = RE::TESObjectREFR;
        static inline void thunk(Target* a_ref)
        {
            func(a_ref);
            WindowSync::ProcessReference(a_ref);
            return;
        }
        static inline void post_hook()
        {
            logger::info("InitItemImpl_TESObjectREFR hook installed");
        }
        static inline REL::Relocation<decltype(thunk)> func;
        static inline constexpr VariantIndex index = VariantIndex(0x13);
    };
    void Install()
    {
        stl::install_hook<InitItemImpl_TESObjectREFR>();
    };
}  // namespace MPL::Hooks
