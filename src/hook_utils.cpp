/**
 * FluffyTail - process signature and owned-pointer patch helpers.
 *
 * Copyright (c) 2026 Aeshur. GNU LGPL v3. See LICENSE.md.
 */

#include "hook_utils.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{

auto readable_region(const MEMORY_BASIC_INFORMATION& info) -> bool
{
    constexpr DWORD BLOCKED = PAGE_GUARD | PAGE_NOACCESS;
    return info.State == MEM_COMMIT && (info.Protect & BLOCKED) == 0;
}

auto patch_pointer(void** slot, void* expected, void* replacement)
    -> fluffytail::slot_patch_result
{
    if (slot == nullptr || (reinterpret_cast<uintptr_t>(slot) % alignof(void*)) != 0)
        return { false, true, nullptr };

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto pageOffset = reinterpret_cast<uintptr_t>(slot) % systemInfo.dwPageSize;
    if (pageOffset > systemInfo.dwPageSize - sizeof(void*))
        return { false, true, nullptr };

    DWORD oldProtect{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
        return { false, false, nullptr };

    auto* prior = InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot), replacement, expected);

    DWORD ignored{};
    bool  protectionRestored =
        VirtualProtect(slot, sizeof(void*), oldProtect, &ignored) != FALSE;
    if (!protectionRestored)
        protectionRestored =
            VirtualProtect(slot, sizeof(void*), oldProtect, &ignored) != FALSE;
    return { prior == expected, protectionRestored, prior };
}

} // namespace

namespace fluffytail
{

auto pin_current_module() noexcept -> bool
{
    HMODULE module{};
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCSTR>(&pin_current_module),
                              &module) != FALSE;
}

auto find_unique_bytes(const uint8_t* begin,
                       size_t         size,
                       const uint8_t* pattern,
                       size_t         pattern_size) -> const uint8_t*
{
    if (begin == nullptr || pattern == nullptr || pattern_size == 0 || pattern_size > size)
        return nullptr;

    const auto* first = std::search(begin, begin + size, pattern, pattern + pattern_size);
    if (first == begin + size)
        return nullptr;

    const auto* second = std::search(first + 1, begin + size, pattern, pattern + pattern_size);
    return second == begin + size ? first : nullptr;
}

auto find_module_signature(const char*    module_name,
                           const uint8_t* pattern,
                           size_t         pattern_size) -> uintptr_t
{
    const auto module = GetModuleHandleA(module_name);
    if (module == nullptr || pattern == nullptr || pattern_size == 0)
        return 0;

    const auto* base = reinterpret_cast<const uint8_t*>(module);
    const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return 0;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage == 0)
        return 0;

    const auto*    imageEnd = base + nt->OptionalHeader.SizeOfImage;
    const uint8_t* match    = nullptr;
    const auto*    sections = IMAGE_FIRST_SECTION(nt);
    for (uint16_t sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections;
         ++sectionIndex)
    {
        const auto& section = sections[sectionIndex];
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
            continue;

        const auto  sectionSize  = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        const auto* sectionBegin = base + section.VirtualAddress;
        const auto* sectionEnd   = (std::min)(imageEnd, sectionBegin + sectionSize);
        for (const uint8_t* cursor = sectionBegin; cursor < sectionEnd;)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(cursor, &info, sizeof(info)) != sizeof(info))
                return 0;

            const auto* regionBegin =
                (std::max)(cursor, static_cast<const uint8_t*>(info.BaseAddress));
            const auto* regionEnd = (std::min)(sectionEnd, static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize);
            if (readable_region(info) && regionEnd > regionBegin &&
                static_cast<size_t>(regionEnd - regionBegin) >= pattern_size)
            {
                const auto* local =
                    std::search(regionBegin, regionEnd, pattern, pattern + pattern_size);
                if (local != regionEnd)
                {
                    if (match != nullptr ||
                        std::search(local + 1, regionEnd, pattern, pattern + pattern_size) !=
                            regionEnd)
                        return 0;
                    match = local;
                }
            }
            cursor = regionEnd;
        }
    }
    return reinterpret_cast<uintptr_t>(match);
}

auto write_executable(uintptr_t address, const void* bytes, size_t size)
    -> executable_patch_result
{
    if (address == 0 || bytes == nullptr || size == 0)
        return { false, false, true };

    DWORD oldProtect{};
    if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return { false, false, false };

    bool bytesWritten = false;
    bool flushed      = false;
    __try
    {
        CopyMemory(reinterpret_cast<void*>(address), bytes, size);
        flushed = FlushInstructionCache(
                      GetCurrentProcess(), reinterpret_cast<void*>(address), size) != FALSE;
        bytesWritten =
            std::memcmp(reinterpret_cast<const void*>(address), bytes, size) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    DWORD ignored{};
    bool  restored = VirtualProtect(
                         reinterpret_cast<void*>(address), size, oldProtect, &ignored) != FALSE;
    if (!restored)
        restored = VirtualProtect(
                       reinterpret_cast<void*>(address), size, oldProtect, &ignored) != FALSE;
    return { bytesWritten, flushed, restored };
}

auto compare_bytes(uintptr_t address, const void* bytes, size_t size) -> bool
{
    if (address == 0 || bytes == nullptr || size == 0)
        return false;
    __try
    {
        return std::memcmp(reinterpret_cast<const void*>(address), bytes, size) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

auto replace_vtable_slot(void** slot, void* expected, void* replacement) -> slot_patch_result
{
    return patch_pointer(slot, expected, replacement);
}

auto restore_vtable_slot(void** slot, void* replacement, void* previous) -> slot_patch_result
{
    return patch_pointer(slot, replacement, previous);
}

} // namespace fluffytail
