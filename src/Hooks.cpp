#include <ClibUtil/editorID.hpp>
#include <Hooks.h>
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
                if (ref != nullptr && (ref->Is(RE::FormType::MovableStatic) || ref->Is(RE::FormType::Static)))
                {
                    auto* std = MPL::Config::StatData::GetSingleton();
                    std->LoadConfig();
                    auto fid = a_ref->GetFormID();
                    auto edid = clib_util::editorID::get_editorID(a_ref);
                    if (!edid.empty())
                    {
                        auto reg = ctre::match<"(?<dyndo>dyndolodes[pm]_[0-9a-fA-F]{6}_)?(?<file_name>.*)(?<type>esm|esl|esp)_(?<lfid>[0-9a-fA-F]{6})_?.*">(edid);
                        if (reg.matched())
                        {
                            auto file_name = reg.get<"file_name">().to_string();
                            auto lfid = strtoul(reg.get<"lfid">().to_string().c_str(), nullptr, 16);
                            auto type = reg.get<"type">().to_string();
                            for (auto* file : RE::TESDataHandler::GetSingleton()->files)
                            {
                                auto rfn = file->GetFilename();
                                std::string nrf(rfn.begin(), rfn.end());
                                std::string fnr(rfn.begin(), rfn.end());
                                fnr.erase(std::remove_if(fnr.begin(), fnr.end(), [](unsigned char c) {
                                    return std::isspace(c) || c == '\'' || c == '-';
                                }),
                                    fnr.end());
                                if (_strcmpi(fnr.c_str(), std::format("{}.{}", file_name, type).c_str()) == 0)
                                {
                                    file_name = nrf;
                                    break;
                                }
                            }
                            if (file_name != "" && type != "" && lfid != 0x0)
                            {
#ifdef DEBUG
                                logger::info("DynDOLOD: Resolving {}:{:06X} -> file: {}, lfid: {:06X} for lookup", a_ref->sourceFiles.array->back()->GetFilename(), a_ref->GetLocalFormID(), file_name, lfid);
#endif
                                fid = RE::TESDataHandler::GetSingleton()->LookupFormID(lfid, file_name);
#ifdef DEBUG
                                logger::info("DynDOLOD Resolved->lfid: {}:{:06X}", file_name, fid);
#endif
                            }
                        }
#ifdef DEBUG
                        else
                        {
                            logger::info("Other: {}", edid);
                        }
#endif
                    }
                    if (fid != 0x0)
                    {
                        MPL::Config::ConfigEntry* itm = nullptr;
                        for (auto& ent : std->entries | std::views::reverse)
                        {
                            if (ent.forms.count(fid))
                            {
                                itm = &ent;
                                break;
                            }
                        }
                        if (itm != nullptr && itm->xemi != 0x0 && !(a_ref->sourceFiles.array->back()->GetFilename().starts_with("WSU") || a_ref->sourceFiles.array->back()->GetFilename() == "Synthesis.esp"))
                        {
                            if (a_ref->extraList.HasType<RE::ExtraEmittanceSource>())
                            {
                                auto* edr = a_ref->extraList.GetByType<RE::ExtraEmittanceSource>();
                                auto* frm = RE::TESForm::LookupByID(itm->xemi);
#ifdef DEBUG
                                logger::info("(INIT)(REP): {:X}:{} -> {:X}:{} W/ {:X}:{}", a_ref->GetLocalFormID(), a_ref->sourceFiles.array->front()->GetFilename(), edr->source->GetLocalFormID(), edr->source->sourceFiles.array->front()->GetFilename(), frm->GetLocalFormID(), frm->sourceFiles.array->front()->GetFilename());
#endif
                                edr->source = frm;
                            }
                            else
                            {
                                auto* frm = RE::TESForm::LookupByID(itm->xemi);
#ifdef DEBUG
                                logger::info("(INIT)(CRE): {:X}:{} -> {:X}:{}", a_ref->GetLocalFormID(), a_ref->sourceFiles.array->front()->GetFilename(), frm->GetLocalFormID(), frm->sourceFiles.array->front()->GetFilename());
#endif
                                auto* ext = RE::BSExtraData::Create<RE::ExtraEmittanceSource>();
                                ext->source = frm;
                                a_ref->extraList.Add(ext);
                            }
                        }
                    }
                }
            }
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