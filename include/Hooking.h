#pragma once
#include <cstdint>
#include <format>

#define ByteAt(addr) *reinterpret_cast<std::uint8_t*>(addr)

class VariantIndex
{
private:
    size_t a_se;
    size_t a_ae;
    size_t a_vr;

public:
    constexpr VariantIndex() noexcept = default;
    explicit constexpr VariantIndex(size_t universal) noexcept
    {
        a_se = a_ae = a_vr = universal;
    }
    explicit constexpr VariantIndex(size_t se, size_t ae) noexcept
    {
        a_vr = a_se = se;
        ae = ae;
    }
    explicit constexpr VariantIndex(size_t se, size_t ae, size_t vr) noexcept
    {
        a_se = se;
        a_ae = ae;
        a_vr = vr;
    }
    size_t index() const noexcept
    {
        switch (REL::Module::GetRuntime())
        {
        case REL::Module::Runtime::AE:
            return a_ae;
        case REL::Module::Runtime::SE:
            return a_se;
        case REL::Module::Runtime::VR:
            return a_vr;
        default:
            return a_ae;
        }
    }
};

/// Declraing a pre_hook function allows Hook to receive a call before the main
/// hook will be installed.
template <typename Hook>
concept pre_hook = requires {
    { Hook::pre_hook() };
};

/// Declraing a post_hook function allows Hook to receive a call immediately
/// after the main hook will be installed.
template <typename Hook>
concept post_hook = requires {
    { Hook::post_hook() };
};

/// Fundamental concept for a hook.
/// A hook must have a static thunk function that will be written to a
/// trampoline.
template <typename Hook>
concept hook = requires {
    { Hook::thunk };
};

template <typename Hook>
concept custom_install = hook<Hook> && requires {
    { Hook::install };
};

/// Optionally Hook can define a static member named func that will contain the
/// original function to chain the call. static inline
/// REL::Relocation<decltype(thunk)> func;
template <typename Hook>
concept chain_hook = requires {
    { Hook::func };
};

/// Basic Hook that writes a call (write_call<5>) instruction to a thunk.
/// This also supports writing to lea instructions, which store function
/// addresses.
template <typename Hook>
concept call_hook = hook<Hook> && requires {
    { Hook::relocation } -> std::convertible_to<REL::VariantID>;
    { Hook::offset } -> std::convertible_to<REL::VariantOffset>;
};

template <typename Hook>
concept addr_hook = hook<Hook> && requires {
    { Hook::addr } -> std::convertible_to<REL::VariantID>;
};

/// A type that has a vtable to hook into.
/// vtable_hook can only be used with Targets that have a vtable.
template <typename Target>
concept has_vtable = requires {
    { Target::VTABLE };
};

/// Defines required fields for a valid vtable hook.
/// Note that providing a custom vtable index is optional, if ommited `0`th
/// table will be used by default.
template <typename Hook>
concept vtable_hook = hook<Hook> && requires {
    { Hook::index } -> std::convertible_to<VariantIndex>;
    requires(has_vtable<typename Hook::Target>);
};

template <typename Hook>
concept offset_vtable_hook = hook<Hook> && vtable_hook<Hook> && requires {
    { Hook::offset } -> std::convertible_to<REL::VariantOffset>;
};

/// Allows to provide a custom vtable index for a vtable hook.
/// Note that providing a custom vtable index is optional, if ommited `0`th
/// table will be used by default.
template <typename Hook>
concept custom_vtable_index = requires {
    { Hook::vtable } -> std::convertible_to<std::size_t>;
};

// Optional properties of a hook.
namespace details
{
    template <typename Hook>
    constexpr std::size_t get_vtable()
    {
        if constexpr (custom_vtable_index<Hook>)
        {
            return Hook::vtable;  // Use the vtable if it exists
        }
        else
        {
            return 0;  // Default to 0 if vtable doesn't exist
        }
    }

    template <typename Hook>
    constexpr void set_func(std::uintptr_t func)
    {
        if constexpr (chain_hook<Hook>)
        {
            Hook::func = func;
        }
    }
}  // namespace details

namespace stl
{
    using namespace SKSE::stl;

    template <hook Hook>
    void write_thunk_call(std::uintptr_t a_src)
    {
        auto& trampoline = SKSE::GetTrampoline();
        SKSE::AllocTrampoline(14);
        details::set_func<Hook>(trampoline.write_call<5>(a_src, Hook::thunk));
    }

    template <has_vtable F, typename Hook>
    void write_vfunc()
    {
        REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[details::get_vtable<Hook>()] };
        details::set_func<Hook>(vtbl.write_vfunc(Hook::index.index(), Hook::thunk));
    }

    template <addr_hook Hook>
    void write_addr()
    {
        auto res = *reinterpret_cast<uintptr_t*>(Hook::addr.address());
        details::set_func<Hook>(res);
        REL::safe_write(Hook::addr.address(), reinterpret_cast<uintptr_t>(&Hook::thunk));
    }

    template <vtable_hook Hook>
    void write_vfunc()
    {
        write_vfunc<typename Hook::Target, Hook>();
    }

    template <call_hook Hook>
    void write_thunk()
    {
        const REL::Relocation<std::uintptr_t> rel{ Hook::relocation, Hook::offset };
        std::uintptr_t sourceAddress = rel.address();

        auto byteAddress = sourceAddress;
        auto opcode = ByteAt(byteAddress);
        if (opcode == 0xE8)
        {  // CALL instruction
            write_thunk_call<Hook>(sourceAddress);
        }
        else
        {
            auto leaSize = 7;
            constexpr std::uint8_t rexw = 0x48;
            if ((opcode & rexw) !=
                rexw)
            {  // REX.W Must be present for a valid 64-bit address replacement.
                stl::report_and_fail(
                    "Invalid hook location, lea instruction must use 64-bit register (first byte should be between 0x48 and 04F)"sv);
            }
            opcode = ByteAt(++byteAddress);
            if (opcode == 0x8D)
            {                                      // LEA instruction
                auto op1 = ByteAt(++byteAddress);  // Get first operand byte.
                auto opAddress = byteAddress;
                // Store original displacement
                std::int32_t disp = 0;
                for (std::uint8_t i = 0; i < 4; ++i)
                {
                    disp |= ByteAt(++byteAddress) << (i * 8);
                }

                assert(disp != 0);
                // write CALL on top of LEA
                // This will fill new displacement
                // 8D MM XX XX XX XX -> 8D E8 YY YY YY YY (where MM is the operand #1, XX
                // is the old func, and YY is the new func)
                write_thunk_call<Hook>(opAddress);

                // Restore operand byte
                // Since we overwrote first operand of lea we need to write it back
                // 8D E8 YY YY YY YY -> 8D MM YY YY YY YY
                REL::safe_write(opAddress, op1);

                // Find original function and store it in the hook's func.
                details::set_func<Hook>(sourceAddress + leaSize + disp);
            }
            else
            {
                stl::report_and_fail(
                    "Invalid hook location, write_thunk can only be used for call or lea instructions"sv);
            }
        }
    }

    template <class Hook>
    void write_offset_vtable()
    {
        auto vtable = 0;
        if constexpr (custom_vtable_index<Hook>)
        {
            vtable = Hook::vtable;
        }
        REL::Relocation<uintptr_t> vtbl{ Hook::Target::VTABLE[vtable] };
        auto func = reinterpret_cast<std::uintptr_t*>(vtbl.address())[Hook::index.index()];
        auto callSite = func + Hook::offset.offset();
        logger::info("Function address: 0x{:X}", func);
        if (auto opcode = *reinterpret_cast<std::uint8_t*>(callSite); opcode != 0xE8)
        {
            stl::report_and_fail(std::format("Expected a CALL (0xE8) at {:X} for offset but found {:X};\ncrashing to avoid corrupting code. The call-site offset may have \nchanged in this game version.", callSite, opcode));
            return;
        }
        stl::write_thunk_call<Hook>(callSite);
    }

    /// Installs given hook
    template <hook Hook>
    void install_hook()
    {
        using ThunkType = decltype(Hook::thunk);
        if constexpr (chain_hook<Hook>)
        {
            using FuncType = decltype(Hook::func);
            static_assert(
                std::is_same_v<REL::Relocation<ThunkType>, FuncType>,
                "Mismatching type of thunk and func. 'Use static inline "
                "REL::Relocation<decltype(thunk)> func;' to always match the type.");
        }

        if constexpr (pre_hook<Hook>)
        {
            Hook::pre_hook();
        }
        if constexpr (custom_install<Hook>)
        {
            Hook::install();
        }
        else if constexpr (offset_vtable_hook<Hook>)
        {
            stl::write_offset_vtable<Hook>();
        }
        else if constexpr (call_hook<Hook>)
        {
            stl::write_thunk<Hook>();
        }
        else if constexpr (vtable_hook<Hook>)
        {
            stl::write_vfunc<Hook>();
        }
        else if constexpr (addr_hook<Hook>)
        {
            stl::write_addr<Hook>();
        }
        else
        {
            static_assert(
                false,
                "Unsupported hook type. Hook must target either call, lea or vtable");
        }

        if constexpr (post_hook<Hook>)
        {
            Hook::post_hook();
        }
    }
}  // namespace stl

#undef ByteAt
