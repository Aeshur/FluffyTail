/**
 * FluffyTail - deterministic native policy and hook-helper tests.
 *
 * Copyright (c) 2026 Aeshur. See LICENSES.md.
 */

#include "hook_utils.hpp"
#include "tail_policy.hpp"
#include "windower.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{

auto fail(const char* message) -> int
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main(int argc, char** argv) -> int
{
    constexpr std::array<const char*, 16> EXPECTED = {
        "red",
        "brunette",
        "white",
        "silver",
        "silver",
        "red",
        "blonde",
        "red",
        "rose",
        "brunette",
        "white",
        "red",
        "blonde",
        "brunette",
        "brunette",
        "blonde",
    };
    for (uint8_t hair = 0; hair < EXPECTED.size(); ++hair)
    {
        if (std::strcmp(fluffytail::face_colour(hair), EXPECTED[hair]) != 0)
            return fail("face colour map changed");
    }
    if (fluffytail::face_colour(16) != nullptr)
        return fail("out-of-range hair mapped to a colour");
    if (fluffytail::rgb565(fluffytail::BASELINE_RGB) != 0x20E3)
        return fail("neutral RGB565 endpoint changed");

    constexpr uint8_t UNIQUE_DATA[]    = { 1, 2, 3, 4, 5, 6, 7 };
    constexpr uint8_t UNIQUE_PATTERN[] = { 3, 4, 5 };
    constexpr uint8_t DUPLICATE_DATA[] = { 3, 4, 5, 3, 4, 5 };
    if (fluffytail::find_unique_bytes(UNIQUE_DATA,
                                      sizeof(UNIQUE_DATA),
                                      UNIQUE_PATTERN,
                                      sizeof(UNIQUE_PATTERN)) != UNIQUE_DATA + 2)
        return fail("unique signature was not found");
    if (fluffytail::find_unique_bytes(DUPLICATE_DATA,
                                      sizeof(DUPLICATE_DATA),
                                      UNIQUE_PATTERN,
                                      sizeof(UNIQUE_PATTERN)) != nullptr)
        return fail("duplicate signature was accepted");

    void*      slots[2]  = { reinterpret_cast<void*>(1), reinterpret_cast<void*>(2) };
    const auto installed = fluffytail::replace_vtable_slot(
        &slots[1], reinterpret_cast<void*>(2), reinterpret_cast<void*>(3));
    if (!installed.exchanged || !installed.protection_restored ||
        installed.observed != reinterpret_cast<void*>(2))
        return fail("owned vtable replacement failed");
    const auto conflicted = fluffytail::replace_vtable_slot(
        &slots[1], reinterpret_cast<void*>(2), reinterpret_cast<void*>(4));
    if (conflicted.exchanged || conflicted.observed != reinterpret_cast<void*>(3))
        return fail("vtable replacement overwrote a foreign owner");
    const auto restored = fluffytail::restore_vtable_slot(
        &slots[1], reinterpret_cast<void*>(3), reinterpret_cast<void*>(2));
    if (!restored.exchanged || !restored.protection_restored)
        return fail("owned vtable restoration failed");

    constexpr std::array<uint8_t, 4> ORIGINAL_CODE = { 0x90, 0x90, 0x90, 0xC3 };
    constexpr std::array<uint8_t, 4> PATCHED_CODE  = { 0xCC, 0x90, 0x90, 0xC3 };
    auto*                            executable    = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, ORIGINAL_CODE.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (executable == nullptr)
        return fail("executable patch fixture allocation failed");
    std::memcpy(executable, ORIGINAL_CODE.data(), ORIGINAL_CODE.size());
    DWORD ignored{};
    if (VirtualProtect(executable, ORIGINAL_CODE.size(), PAGE_EXECUTE_READ, &ignored) == FALSE)
    {
        VirtualFree(executable, 0, MEM_RELEASE);
        return fail("executable patch fixture protection failed");
    }
    const auto patched = fluffytail::write_executable(
        reinterpret_cast<uintptr_t>(executable), PATCHED_CODE.data(), PATCHED_CODE.size());
    if (!patched.bytes_written || !patched.cache_flushed || !patched.protection_restored ||
        std::memcmp(executable, PATCHED_CODE.data(), PATCHED_CODE.size()) != 0)
    {
        VirtualFree(executable, 0, MEM_RELEASE);
        return fail("executable patch result was incomplete");
    }
    const auto codeRestored = fluffytail::write_executable(
        reinterpret_cast<uintptr_t>(executable), ORIGINAL_CODE.data(), ORIGINAL_CODE.size());
    if (!codeRestored.bytes_written || !codeRestored.cache_flushed ||
        !codeRestored.protection_restored ||
        std::memcmp(executable, ORIGINAL_CODE.data(), ORIGINAL_CODE.size()) != 0)
    {
        VirtualFree(executable, 0, MEM_RELEASE);
        return fail("executable patch restoration was incomplete");
    }
    VirtualFree(executable, 0, MEM_RELEASE);

    static_assert(fluffytail::windower::INTERFACE_VERSION == 0x04070300,
                  "Windower interface version changed");
    static_assert(sizeof(fluffytail::windower::PluginBase) == sizeof(void*),
                  "Windower PluginBase layout changed");

    if (argc != 2)
        return fail("native test requires the dual-host DLL path");

    const auto module = LoadLibraryA(argv[1]);
    if (module == nullptr)
        return fail("dual-host DLL did not load");

    using get_windower_version_fn = uint32_t(__cdecl*)();
    using create_windower_fn      = fluffytail::windower::PluginBase*(__cdecl*)();
    using create_ashita_fn        = void*(__stdcall*)(const char*);
    using destroy_ashita_fn       = void(__stdcall*)(void*);
    using get_ashita_version_fn   = double(__stdcall*)();

    const auto getWindowerVersion = reinterpret_cast<get_windower_version_fn>(
        GetProcAddress(module, "GetInterfaceVersion"));
    const auto createWindower = reinterpret_cast<create_windower_fn>(
        GetProcAddress(module, "CreateInstance"));
    const auto createAshita =
        reinterpret_cast<create_ashita_fn>(GetProcAddress(module, "expCreatePlugin"));
    const auto destroyAshita =
        reinterpret_cast<destroy_ashita_fn>(GetProcAddress(module, "expDestroyPlugin"));
    const auto getAshitaVersion = reinterpret_cast<get_ashita_version_fn>(
        GetProcAddress(module, "expGetInterfaceVersion"));
    if (getWindowerVersion == nullptr || createWindower == nullptr || createAshita == nullptr ||
        destroyAshita == nullptr || getAshitaVersion == nullptr)
    {
        FreeLibrary(module);
        return fail("dual-host export set is incomplete");
    }
    if (getWindowerVersion() != fluffytail::windower::INTERFACE_VERSION)
    {
        FreeLibrary(module);
        return fail("Windower export returned the wrong interface version");
    }

    auto* windowerPlugin = createWindower();
    auto* ashitaPlugin   = createAshita("");
    if (windowerPlugin == nullptr || ashitaPlugin == nullptr)
    {
        if (windowerPlugin != nullptr)
            windowerPlugin->Dtor(1);
        if (ashitaPlugin != nullptr)
            destroyAshita(ashitaPlugin);
        FreeLibrary(module);
        return fail("host adapter construction failed");
    }
    windowerPlugin->Dtor(1);
    auto* nonDeletingPlugin = createWindower();
    if (nonDeletingPlugin == nullptr || nonDeletingPlugin->Dtor(0) != nonDeletingPlugin)
    {
        destroyAshita(ashitaPlugin);
        FreeLibrary(module);
        return fail("Windower non-deleting destructor contract failed");
    }
    destroyAshita(ashitaPlugin);
    if (getAshitaVersion() <= 0.0)
    {
        FreeLibrary(module);
        return fail("Ashita export returned an invalid interface version");
    }
    FreeLibrary(module);

    std::cout << "native policy, hook helpers, exports, and adapter construction checks passed\n";
    return 0;
}
