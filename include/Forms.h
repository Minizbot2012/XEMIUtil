#pragma once
namespace MPL::Config
{
    struct LiteForm
    {
        RE::FormID formID;
        template <class T>
        T* Get()
        {
            return RE::TESForm::LookupByID<T>(formID);
        };
        static LiteForm FromID(RE::FormID id) { return { .formID = id }; };
        bool operator==(const LiteForm& other) const { return formID == other.formID; }
    };
}  // namespace MPL::Config

namespace std
{
    template <>
    struct hash<MPL::Config::LiteForm>
    {
        std::size_t operator()(const MPL::Config::LiteForm& k) const noexcept
        {
            return std::hash<RE::FormID>()(k.formID);
        }
    };
}  // namespace std

namespace rfl
{
    template <>
    struct Reflector<MPL::Config::LiteForm>
    {
        using ReflType = std::string;
        static MPL::Config::LiteForm to(const ReflType& v)
        {
            if (v == "null")
            {
                return MPL::Config::LiteForm::FromID(0x0);
            }
            auto loc = v.find(":");
            if (loc != std::string::npos)
            {
                auto lfid = strtoul(v.substr(0, loc).c_str(), nullptr, 16);
                auto file = v.substr(loc + 1);
                auto* dh = RE::TESDataHandler::GetSingleton();
                return MPL::Config::LiteForm::FromID(dh->LookupFormID(lfid, file));
            }
            else
            {
                auto* frm = RE::TESForm::LookupByEditorID(v);
                if (frm)
                {
                    return MPL::Config::LiteForm::FromID(frm->formID);
                }
                else
                {
                    return MPL::Config::LiteForm::FromID(0x0);
                }
            }
        }
    };
}  // namespace rfl
