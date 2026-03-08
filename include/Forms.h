#pragma once
#include <RE/T/TESDataHandler.h>
#include <functional>
namespace MPL::Config
{
    struct Form
    {
        RE::FormID formID;
        RE::TESForm* operator*()
        {
            return RE::TESForm::LookupByID(formID);
        };
        RE::TESForm* operator->()
        {
            return RE::TESForm::LookupByID(formID);
        };
        bool operator==(const Form& other) const
        {
            return formID == other.formID;
        };
        RE::FormID GetFormID() const
        {
            return formID;
        };
        bool IsNull() const
        {
            return formID == 0x0 || RE::TESForm::LookupByID(formID) == nullptr;
        };
        static Form FromFormID(const RE::FormID fid)
        {
            return Form{
                .formID = fid,
            };
        };
        static Form FromForm(RE::TESForm* frm)
        {
            return Form{
                .formID = frm->GetFormID(),
            };
        };
    };
}  // namespace MPL::Config

namespace std
{
    template <>
    struct hash<MPL::Config::Form>
    {
        std::size_t operator()(const MPL::Config::Form& f) const noexcept
        {
            return std::hash<std::uint32_t>{}(f.formID);
        };
    };
}  // namespace std

namespace rfl
{
    template <>
    struct Reflector<MPL::Config::Form>
    {
        using ReflType = std::string;
        static ReflType from(const RE::TESForm* const& form)
        {
            return std::format("{:06X}:{}", form->GetLocalFormID(), form->GetFile(0)->GetFilename());
        }
        static MPL::Config::Form to(const ReflType& v)
        {
            if(v == "null") {
                return MPL::Config::Form::FromFormID(0x0);
            }
            auto loc = v.find(":");
            if (loc != std::string::npos)
            {
                auto lfid = strtoul(v.substr(0, loc).c_str(), nullptr, 16);
                auto file = v.substr(loc + 1);
                auto dh = RE::TESDataHandler::GetSingleton();
                auto frm = dh->LookupFormID(lfid, file);
                return MPL::Config::Form::FromFormID(frm);
            }
            else
            {
                auto frm = RE::TESForm::LookupByEditorID(v);
                if (frm)
                {
                    return MPL::Config::Form::FromForm(frm);
                }
                else
                {
                    return MPL::Config::Form::FromFormID(0x0);
                }
            }
        }
    };
}  // namespace rfl
