#!/usr/bin/env bash
# Run inside the pinned toolchain image, with a fresh committed-source export
# mounted at /switch-mesa and a dedicated writable output directory at /work.
set -euo pipefail
readonly MESA_COMMIT=0ff52079ca9e9e837ed27eb95a8d239f439a1864
: "${JOBS:=4}"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:?pass the pinned commit timestamp}"
export ZERO_AR_DATE=1 LC_ALL=C TZ=UTC
export TMPDIR=/work/tmp
export MESA_SWITCH_RUST_TARGET=aarch64-unknown-linux-gnu
export PKG_CONFIG_LIBDIR=/opt/devkitpro/portlibs/switch/lib/pkgconfig
export DEVKITA64=/opt/devkitpro/devkitA64
export LLVM_CONFIG=/usr/lib/llvm-15/bin/llvm-config
mkdir -p /work/tmp /work/evidence
if [[ -n "${HOST_UID:-}" && -n "${HOST_GID:-}" ]]; then
  trap 'chown -R "$HOST_UID:$HOST_GID" /work' EXIT
fi
mkdir -p /work/evidence/toolchain
cp -a /usr/local/share/mesa-sdk-toolchain/. /work/evidence/toolchain/
{
  rustc -Vv
  meson --version
  bindgen --version
  cbindgen --version
  "$DEVKITA64/bin/aarch64-none-elf-gcc" --version
  cat /etc/os-release
} > /work/evidence/tool-versions.txt
# Read native pkg-config metadata while building host-only shader generators.
unset PKG_CONFIG_LIBDIR
export PKG_CONFIG_PATH=/work/host-prefix/lib/pkgconfig
export CMAKE_PREFIX_PATH=/work/host-prefix
if [[ ! -f /work/native/meson-private/coredata.dat ]]; then
  meson setup /work/native /switch-mesa --buildtype=release \
    -Dvulkan-drivers= -Dgallium-drivers= -Dshader-cache=disabled \
    -Dplatforms= -Dglx=disabled -Degl=disabled -Dopengl=false \
    -Dgles1=disabled -Dgles2=disabled -Dtools=[] \
    -Dllvm=enabled -Dmesa-clc=enabled -Dprecomp-compiler=enabled \
    -Dinstall-mesa-clc=true -Dmesa-clc-bundle-headers=enabled
else
  meson configure /work/native -Dmesa-clc-bundle-headers=enabled
fi
ninja -C /work/native -j "$JOBS" \
  src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2
export PATH="/work/native/src/compiler/clc:/work/native/src/compiler/spirv:$PATH"
unset PKG_CONFIG_PATH CMAKE_PREFIX_PATH
export PKG_CONFIG_LIBDIR=/opt/devkitpro/portlibs/switch/lib/pkgconfig
# Upstream's checked-in Linux file names wrappers absent from Docker.rust.
# Change only those executable paths; keep all target/compiler/ABI flags.
# Meson 1.9 calls its compile-only Rust probe meson-private/sanity.rs.
# Upstream already runs those probes natively, but recognizes older filenames.
python3 -c 'from pathlib import Path; s=Path("/switch-mesa/rustc-switch-wrapper.sh").read_text(); old="*sanity_check_for_rust.rs*|*sanitycheckrs.rs*)"; assert s.count(old)==1; p=Path("/work/rustc-switch-wrapper.sh"); p.write_text(s.replace(old,"*sanity_check_for_rust.rs*|*sanitycheckrs.rs*|*/meson-private/sanity.rs)")); p.chmod(0o755)'
python3 -c 'from pathlib import Path; s=Path("/switch-mesa/switch_cross_file.txt").read_text(); assert s.count("/usr/local/libexec/rustc")==1 and s.count("/usr/local/libexec/bindgen")==1; s=s.replace("/usr/local/libexec/rustc", "/work/rustc-switch-wrapper.sh").replace("/usr/local/libexec/bindgen", "/switch-mesa/bindgen-switch-wrapper.sh"); Path("/work/cross.ini").write_text(s)'
if [[ ! -f /work/target/meson-private/coredata.dat ]]; then
  meson setup /work/target /switch-mesa \
    --cross-file /work/cross.ini \
    --default-library=static --prefix=/opt/devkitpro/portlibs/switch --libdir=lib \
    --buildtype=release -Db_ndebug=true -Db_lto=false \
    -Dvulkan-drivers=nouveau -Dgallium-drivers= -Dplatforms=switch \
    -Dglx=disabled -Degl=disabled -Dopengl=false \
    -Dgles1=disabled -Dgles2=disabled -Dvideo-codecs= \
    -Dshader-cache=disabled -Dxmlconfig=enabled -Dexpat=enabled \
    -Dtools=[] -Dllvm=disabled -Dmesa-clc=system -Dprecomp-compiler=system \
    -Dcpp_rtti=false -Dbuild-tests=false -Dnvk-build-id="$MESA_COMMIT"
fi
ninja -C /work/target -t commands > /work/evidence/target-commands.txt
ninja -C /work/target -j "$JOBS"
# Never remove or reuse an existing SDK stage.
if [[ -e /work/stage ]]; then
  printf '%s\n' 'Refusing to overwrite existing /work/stage' >&2
  exit 2
fi
meson install -C /work/target --no-rebuild --destdir /work/stage
meson introspect /work/target --buildoptions > /work/evidence/build-options.json
meson introspect /work/target --dependencies > /work/evidence/build-dependencies.json
