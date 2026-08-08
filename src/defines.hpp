/**
 * FluffyTail - Windows and Ashita SDK integration definitions.
 *
 * Adapted from the Ashita v4 screenshot plugin (src/defines.hpp),
 * Copyright (c) 2025 Ashita Development Team, GNU GPL v3. See LICENSE.GPL.txt.
 */

#pragma once

// The SDK uses zero-sized trailing arrays and can produce long decorated names.
#pragma warning(disable : 4200)
#pragma warning(disable : 4503)

// Windows compiling configurations.
#define NOMINMAX
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN

// Windows and standard headers.
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Ashita.h"

template <typename T>
auto safe_release(T*& resource) -> void
{
    if (resource != nullptr)
    {
        resource->Release();
        resource = nullptr;
    }
}
