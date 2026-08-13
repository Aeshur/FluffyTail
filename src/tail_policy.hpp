/**
 * FluffyTail - host-independent Mithra face and tail colour policy.
 *
 * Copyright (c) 2026 Aeshur. GNU LGPL v3. See LICENSE.md.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace fluffytail
{

constexpr uint8_t  RACE_MITHRA  = 7;
constexpr uint32_t BASELINE_RGB = 0x241C1A;

struct colour_t
{
    const char* name;
    uint32_t    rgb;
};

inline constexpr const char* FACE_COLOURS[16] = {
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

// These values were calibrated manually in game. Do not change one without
// repeating that calibration, because FFXI lighting brightens the texture.
inline constexpr colour_t COLOURS[] = {
    { "white", 0xD8D8CA },
    { "silver", 0xCDC2D4 },
    { "blonde", 0xC3A261 },
    { "red", 0x652708 },
    { "rose", 0x814231 },
    { "brunette", 0x3E2412 },
};

inline constexpr auto face_colour(uint8_t hair) -> const char*
{
    return hair < (sizeof(FACE_COLOURS) / sizeof(FACE_COLOURS[0])) ? FACE_COLOURS[hair]
                                                                   : nullptr;
}

inline constexpr auto rgb565(uint32_t rgb) -> uint16_t
{
    return static_cast<uint16_t>((((rgb >> 19) & 0x1F) << 11) | (((rgb >> 10) & 0x3F) << 5) |
                                 ((rgb >> 3) & 0x1F));
}

} // namespace fluffytail
