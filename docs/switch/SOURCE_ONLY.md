# Experimental source-only integration fixture

This branch preserves a standalone deko3d probe, structural DKSH validator,
CPU-only host-check configuration, and a dependency-preparation utility.
It is not a working Dusklight/Dawn game renderer, an accepted texture-upload
fix, a binary release, or evidence of GPU execution. Existing public game
source and submodule identities are unchanged. Third-party notices take
precedence over the root dedication for their respective components.

Private logs, receipts, artifact manifests and historical workspace reports
are intentionally not included. No claim relies on an omitted log. A private
source mapping is retained separately, not in this Git history.

Reference pins and installed-toolchain identities: `source-pins.json`.
The source reference deko3d revision is not presumed identical to the installed
validation archive. Submodule URLs and gitlinks remain the exact public pins;
no local dependency override is committed.

Focused checks (from the repository root; outputs outside the checkout):

```sh
python3 -B tests/switch/test_prepare_dependency.py
c++ -std=c++20 -Wall -Wextra -Werror -Iplatforms/switch/probe tests/switch/dksh_layout_test.cpp -o "$OUTPUT/dksh-layout-tests"
"$OUTPUT/dksh-layout-tests"
```

The DKSH check uses authored structural bytes only; it neither compiles shaders
nor executes a graphics implementation. The broader host-check CMake project
needs the pinned Aurora submodule, SDL and googletest sources. It is not part
of the minimal dependency-free structural check above.
