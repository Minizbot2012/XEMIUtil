#include <ClibUtil/editorID.hpp>
#include <Config.h>
#include <Hooks.h>
namespace MPL::Hooks
{
    struct InitItemImpl_TESObjectREFR
    {
        using Target = RE::TESObjectREFR;
        static inline void thunk(Target* a_ref)
        {
            func(a_ref);
            if (a_ref != nullptr)
            {
                auto ref = a_ref->GetObjectReference();
                auto cll = a_ref->GetParentCell();
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
                            if (ent.allowed_cells && (cll == nullptr || !ent.allowed_cells.value().count(MPL::Config::LiteForm::FromID(cll->formID))))
                            {
                                continue;
                            }
                            if (ent.forms.count(MPL::Config::LiteForm::FromID(fid)))
                            {
                                itm = &ent;
                                break;
                            }
                            if (bid != 0x0 && ent.forms_are_base.value_or(false))
                            {
                                if (ent.forms.count(MPL::Config::LiteForm::FromID(bid)))
                                {
                                    itm = &ent;
                                    break;
                                }
                            }
                        }
                        if (itm != nullptr)
                        {
                            if (itm->only_interior.value_or(false))
                            {
                                if (cll == nullptr || !a_ref->parentCell->IsInteriorCell()) goto ret;
                            }
                            if (a_ref->extraList.HasType<RE::ExtraEmittanceSource>() && itm->xemi.formID != 0x0)
                            {
                                auto* edr = a_ref->extraList.GetByType<RE::ExtraEmittanceSource>();
                                auto* frm = itm->xemi.Get<RE::TESForm>();
                                if (frm != nullptr)
                                {
#ifdef DEBUG
                                    logger::info("(INIT)(REP): {:X}:{} -> {:X}:{} W/ {:X}:{}", a_ref->GetLocalFormID(), a_ref->sourceFiles.array->front()->GetFilename(), edr->source->GetLocalFormID(), edr->source->sourceFiles.array->front()->GetFilename(), frm->GetLocalFormID(), frm->sourceFiles.array->front()->GetFilename());
#endif
                                    edr->source = frm;
                                }
                            }
                            else if (itm->xemi.formID != 0x0)
                            {
                                auto* frm = itm->xemi.Get<RE::TESForm>();
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
                            else if (itm->xemi.formID == 0x0 && itm->remove.value_or(false))
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
            return;
        }
        static inline void post_hook()
        {
            logger::info("InitItemImpl_TESObjectREFR hook installed");
        }
        static inline REL::Relocation<decltype(thunk)> func;
        static inline constexpr std::size_t index{ 0x13 };
    };
    void Install()
    {
        stl::install_hook<InitItemImpl_TESObjectREFR>();
    };
}  // namespace MPL::Hooks
