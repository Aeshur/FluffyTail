/**
 * FluffyTail - process signature and owned-pointer patch helpers.
 *
 * Copyright (c) 2026 Aeshur. GNU LGPL v3. See LICENSE.md.
 */

#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fluffytail
{

struct slot_patch_result
{
    bool  exchanged;
    bool  protection_restored;
    void* observed;
};

struct executable_patch_result
{
    bool bytes_written;
    bool cache_flushed;
    bool protection_restored;
};

auto pin_current_module() noexcept -> bool;

auto find_unique_bytes(const uint8_t* begin,
                       size_t         size,
                       const uint8_t* pattern,
                       size_t         pattern_size) -> const uint8_t*;
auto find_module_signature(const char*    module_name,
                           const uint8_t* pattern,
                           size_t         pattern_size) -> uintptr_t;

auto write_executable(uintptr_t address, const void* bytes, size_t size)
    -> executable_patch_result;
auto compare_bytes(uintptr_t address, const void* bytes, size_t size) -> bool;

auto replace_vtable_slot(void** slot, void* expected, void* replacement) -> slot_patch_result;
auto restore_vtable_slot(void** slot, void* replacement, void* previous) -> slot_patch_result;

} // namespace fluffytail
