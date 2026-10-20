#!/usr/bin/env bash
# SPIRV-Tools v2024.1 is Mesa's minimum; Debian 12 supplies only 2023.1.1.
# Mount committed-source exports at /spirv-tools and /spirv-headers.
set -euo pipefail
: "${JOBS:=4}"
export TMPDIR=/work/tmp
mkdir -p "$TMPDIR"
if [[ -n "${HOST_UID:-}" && -n "${HOST_GID:-}" ]]; then
  trap 'chown -R "$HOST_UID:$HOST_GID" /work' EXIT
fi
cmake -S /spirv-tools -B /work/spirv-tools -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/work/host-prefix \
  -DSPIRV-Headers_SOURCE_DIR=/spirv-headers \
  -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON \
  -DSPIRV_TOOLS_BUILD_STATIC=ON -DSPIRV_WERROR=OFF
cmake --build /work/spirv-tools --parallel "$JOBS"
cmake --install /work/spirv-tools
