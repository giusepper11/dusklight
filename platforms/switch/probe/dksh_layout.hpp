// SPDX-License-Identifier: CC0-1.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dusk::switch_probe {

// Only the single-program vertex/fragment format used by this fixture is accepted.
// Layout authority: devkitPro/deko3d source/dksh.h at 373bfd43d8b2d805fa2eb7d406dbb823032df958.
// Structural validation is not proof that machine code is safe or renders correctly.
inline bool validate_dksh(std::span<const std::byte> bytes, uint32_t expectedStage) noexcept {
    constexpr size_t headerSize = 24;
    constexpr size_t programSize = 64;
    if (bytes.size() < headerSize || expectedStage > 1) {
        return false;
    }
    const auto word = [&](size_t offset) -> uint32_t {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= uint32_t(std::to_integer<unsigned char>(bytes[offset + i])) << (8 * i);
        }
        return value;
    };
    if (word(0) != 0x48534b44 || word(4) != headerSize || word(20) != 1) {
        return false;
    }
    const size_t controlSize = word(8);
    const size_t codeSize = word(12);
    const size_t programsOffset = word(16);
    if (controlSize < headerSize || controlSize % 256 || !codeSize || codeSize % 256 ||
        controlSize > bytes.size() || codeSize != bytes.size() - controlSize ||
        programsOffset < headerSize || programsOffset > controlSize ||
        programSize > controlSize - programsOffset) {
        return false;
    }
    if (word(programsOffset) != expectedStage || word(programsOffset + 4) >= codeSize) {
        return false;
    }
    // Nonzero alt_num_gprs activates VertexA, outside this single-program fixture.
    if (expectedStage == 0 && word(programsOffset + 28) != 0) {
        return false;
    }
    const size_t constantsOffset = word(programsOffset + 12);
    const size_t constantsSize = word(programsOffset + 16);
    if (constantsOffset > codeSize || (constantsSize && constantsOffset % 256)) {
        return false;
    }
    // deko3d binds a 256-byte-rounded extent; widen before rounding to avoid overflow.
    const uint64_t roundedConstantsSize = (uint64_t(constantsSize) + 255) & ~uint64_t(255);
    return roundedConstantsSize <= codeSize - constantsOffset;
}

} // namespace dusk::switch_probe
