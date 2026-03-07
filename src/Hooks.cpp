#include <ClibUtil/editorID.hpp>
#include <Config.h>
#include <Hooks.h>
#include <RE/E/ExtraDataTypes.h>
#include <RE/E/ExtraEmittanceSource.h>
namespace MPL::Hooks
{
    struct ShouldBackgroundClone_TESObjectREFR
    {
        using Target = RE::TESObjectREFR;
        static inline bool thunk(Target* a_ref)
        {
            if (a_ref != nullptr)
            {
                auto ref = a_ref->GetObjectReference();
                if (ref != nullptr && (ref->Is(RE::FormType::MovableStatic) || ref->Is(RE::FormType::Static) || ref->Is(RE::FormType::Light)))
                {
                    auto* std = MPL::Config::StatData::GetSingleton();
                    std->LoadConfig();
                    auto fid = a_ref->GetFormID();
                    if (fid != 0x0)
                    {
                        auto bid = 0x0;
                        auto bobj = a_ref->GetBaseObject();
                        if (bobj != nullptr)
                        {
                            bid = bobj->GetFormID();
                        }
                        MPL::Config::ConfigEntry* itm = nullptr;
                        for (auto& ent : std->entries | std::views::reverse)
                        {
                            if (ent.forms.count(MPL::Config::Form::FromFormID(fid)))
                            {
                                itm = &ent;
                                break;
                            }
                            if (bid != 0x0 && ent.forms_are_base.value_or(false))
                            {
                                if (ent.forms.count(MPL::Config::Form::FromFormID(bid)))
                                {
                                    itm = &ent;
                                    break;
                                }
                            }
                        }
                        if ((itm != nullptr && !itm->xemi.IsNull() && !(a_ref->sourceFiles.array->back()->GetFilename().starts_with("WSU") || a_ref->sourceFiles.array->back()->GetFilename() == "Synthesis.esp")))
                        {
                            if (itm->only_interior.value_or(false))
                            {
                                if (!a_ref->parentCell->IsInteriorCell()) goto ret;
                            }
                            if (a_ref->extraList.HasType<RE::ExtraEmittanceSource>() && !itm->xemi.IsNull())
                            {
                                auto* edr = a_ref->extraList.GetByType<RE::ExtraEmittanceSource>();
                                auto* frm = *itm->xemi;
#ifdef DEBUG
                                logger::info("(INIT)(REP): {:X}:{} -> {:X}:{} W/ {:X}:{}", a_ref->GetLocalFormID(), a_ref->sourceFiles.array->front()->GetFilename(), edr->source->GetLocalFormID(), edr->source->sourceFiles.array->front()->GetFilename(), frm->GetLocalFormID(), frm->sourceFiles.array->front()->GetFilename());
#endif
                                if (frm != nullptr)
                                    edr->source = frm;
                            }
                            else if (!itm->xemi.IsNull())
                            {
                                auto* frm = *itm->xemi;
                                if (frm != nullptr)
                                {
#ifdef DEBUG
                                    logger::info("(INIT)(CRE): {:X}:{} -> {:X}:{}", a_ref->GetLocalFormID(), a_ref->sourceFiles.array->front()->GetFilename(), frm->GetLocalFormID(), frm->sourceFiles.array->front()->GetFilename());
#endif
                                    auto* ext = RE::BSExtraData::Create<RE::ExtraEmittanceSource>();
                                    ext->source = frm;
                                    a_ref->extraList.Add(ext);
                                }
                            }
                            else if (itm->xemi.IsNull() && itm->remove.value_or(false))
                            {
                                if (a_ref->extraList.HasType<RE::ExtraEmittanceSource>())
                                {
                                    a_ref->extraList.RemoveByType(RE::ExtraDataType::kEmittanceSource);
                                }
                            }
                        }
                    }
                }
            }
ret:
            return func(a_ref);
        }
        static inline REL::Relocation<decltype(thunk)> func;
        static inline constexpr std::size_t index{ 0x6D };
    };
    void Install()
    {
        stl::install_hook<ShouldBackgroundClone_TESObjectREFR>();
    };
}  // namespace MPL::Hooks
