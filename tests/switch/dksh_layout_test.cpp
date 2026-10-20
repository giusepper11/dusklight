// SPDX-License-Identifier: CC0-1.0
#include "dksh_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

using dusk::switch_probe::validate_dksh;

static void require(bool condition, const char* name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    size_t cases = 0;
    const auto check = [&](bool condition, const char* name) {
        require(condition, name);
        ++cases;
    };
    // Authored structural fixture only; these bytes are NOT executable shader code.
    std::array<std::byte, 1024> bytes{};
    auto put = [&](size_t offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + i] = std::byte((value >> (8 * i)) & 255);
        }
    };
    put(0, 0x48534b44); // DKSH
    put(4, 24);
    put(8, 256);
    put(12, 768);
    put(16, 24);
    put(20, 1);
    put(24, 0); // vertex
    put(28, 0); // entrypoint
    put(36, 512); // aligned constant buffer offset within code
    put(40, 96); // hardware rounds this to 256 bytes
    check(validate_dksh(bytes, 0), "bounded single-program vertex module");
    put(36, 128);
    check(!validate_dksh(bytes, 0), "reject unaligned nonempty constant buffer");
    put(36, 640);
    check(!validate_dksh(bytes, 0), "reject in-bounds raw data with out-of-range rounded extent");
    put(36, 512);
    put(40, 255);
    check(validate_dksh(bytes, 0), "rounded constant extent may end exactly at code boundary");
    put(40, 256);
    check(validate_dksh(bytes, 0), "aligned constant extent may end exactly at code boundary");
    put(40, 257);
    check(!validate_dksh(bytes, 0), "reject rounded constant extent beyond code boundary");
    put(40, 0);
    put(36, 0);
    check(validate_dksh(bytes, 0), "allow absent constant buffer");
    put(36, 127);
    check(validate_dksh(bytes, 0), "zero-sized constant buffer does not require aligned offset");
    put(36, 768);
    check(validate_dksh(bytes, 0), "allow zero-sized constant buffer at code boundary");
    put(36, 769);
    check(!validate_dksh(bytes, 0), "reject zero-sized constant buffer offset beyond code");
    put(36, std::numeric_limits<uint32_t>::max());
    check(!validate_dksh(bytes, 0), "reject maximum constant offset without subtraction underflow");
    put(36, 512);
    put(40, 96);
    put(48, 768); // alternate vertex entrypoint beyond code
    put(52, 1); // nonzero GPR count activates the alternate program
    check(!validate_dksh(bytes, 0), "reject active alternate vertex program with out-of-bounds entrypoint");
    put(48, 0);
    check(!validate_dksh(bytes, 0), "reject even in-bounds active alternate vertex program");
    put(52, 0);
    put(48, std::numeric_limits<uint32_t>::max());
    check(validate_dksh(bytes, 0), "inactive alternate vertex entrypoint is not executed");
    put(48, 0);
    put(24, 1);
    put(52, 1); // fragment metadata shares the vertex alternate-program union
    check(validate_dksh(bytes, 1), "fragment metadata is not an alternate vertex program");
    put(24, 0);
    put(52, 0);
    check(!validate_dksh(bytes, 1), "reject wrong stage");
    check(!validate_dksh(std::span(bytes).first(23), 0), "reject truncated header");
    check(!validate_dksh(std::span(bytes).first(1023), 0), "reject truncated code");
    put(0, 0);
    check(!validate_dksh(bytes, 0), "reject bad magic");
    put(0, 0x48534b44);
    put(4, 20);
    check(!validate_dksh(bytes, 0), "reject unknown header layout");
    put(4, 24);
    put(8, 255);
    check(!validate_dksh(bytes, 0), "reject unaligned control");
    put(8, 256);
    put(12, 255);
    check(!validate_dksh(bytes, 0), "reject unaligned code");
    put(12, 768);
    put(16, 240);
    check(!validate_dksh(bytes, 0), "reject program table beyond control");
    put(16, 0);
    check(!validate_dksh(bytes, 0), "reject table overlapping header");
    put(16, 24);
    put(20, 0);
    check(!validate_dksh(bytes, 0), "reject missing program");
    put(20, std::numeric_limits<uint32_t>::max());
    check(!validate_dksh(bytes, 0), "reject unsupported multiple programs without overflow");
    put(20, 1);
    put(28, 768);
    check(!validate_dksh(bytes, 0), "reject out-of-bounds entrypoint");
    put(28, 0);
    put(36, 767);
    check(!validate_dksh(bytes, 0), "reject constant data extending past code");
    put(36, 512);
    put(40, std::numeric_limits<uint32_t>::max());
    check(!validate_dksh(bytes, 0), "reject overflowing constant data size");
    put(40, 96);
    check(validate_dksh(bytes, 0), "original fixture remains valid");
    std::printf("PASS: %zu DKSH structural cases (not shader execution)\n", cases);
    if (argc != 1 && argc != 3) {
        std::fputs("usage: dksh-layout-test [vertex.dksh fragment.dksh]\n", stderr);
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary | std::ios::ate);
        require(bool(file), "open UAM-generated shader");
        const auto size = file.tellg();
        require(size > 0 && size < 64 * 1024, "bounded UAM-generated shader size");
        std::vector<std::byte> data(static_cast<size_t>(size));
        file.seekg(0);
        require(bool(file.read(reinterpret_cast<char*>(data.data()), data.size())), "read UAM-generated shader");
        check(validate_dksh(data, i - 1), "UAM-generated DKSH structural validation");
        std::printf("PASS: generated shader stage %d: %s (not shader execution)\n", i - 1, argv[i]);
    }
    std::printf("PASS: %zu DKSH validation cases total (not shader execution)\n", cases);
}
