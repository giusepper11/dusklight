#!/usr/bin/env python3
"""Package the verified no-libelf Horizon build; never alter the raw stage."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("dependencies", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--portlibs", type=Path, default=Path("/opt/devkitpro/portlibs/switch"))
    args = parser.parse_args()
    stage = args.stage.resolve(strict=True)
    destination = args.destination.resolve()
    if destination.exists() or args.destination.is_symlink():
        parser.error("destination must be new; failed packages are retained")
    if destination.is_relative_to(stage) or stage.is_relative_to(destination):
        parser.error("stage and destination must be disjoint")
    deps = json.loads(args.dependencies.read_text())
    names = {dep["name"] for dep in deps}
    if not {"expat", "zlib", "libzstd"}.issubset(names):
        parser.error("expected real expat, zlib and zstd dependencies")
    if "libelf" in names or "elf" in names:
        parser.error("this package correction is only for a verified no-libelf build")
    config_relative = Path("lib/cmake/Vulkan/VulkanConfig.cmake")
    config_source = stage / config_relative
    config = config_source.read_text()
    # Upstream disables NVX binary import without libelf on Horizon, but its
    # installed CMake metadata still unconditionally requires libelf. Preserve
    # the original file and correct only that absent, unused dependency.
    for line in (
        "find_library(_VULKAN_SWITCH_ELF_LIBRARY elf)\n",
        "\t\t_VULKAN_SWITCH_ELF_LIBRARY\n",
        '\t"${_VULKAN_SWITCH_ELF_LIBRARY}"\n',
    ):
        if config.count(line) != 1:
            parser.error("unexpected upstream VulkanConfig.cmake; inspect before adapting")
        config = config.replace(line, "")
    copies = []
    for dep in deps:
        if dep["name"] not in {"expat", "zlib", "libzstd"}:
            continue
        for item in dep["link_args"]:
            source = Path(item)
            if source.suffix != ".a" or not source.is_relative_to(args.portlibs):
                parser.error(f"unexpected dependency archive {item}")
            if not source.is_file():
                parser.error(f"missing real dependency archive {item}")
            copies.append((source, Path("lib") / source.name))
    for name in ("expat.h", "expat_external.h", "zstd.h", "zdict.h", "zstd_errors.h", "zlib.h", "zconf.h"):
        source = args.portlibs / "include" / name
        if not source.is_file():
            parser.error(f"missing dependency header {source}")
        copies.append((source, Path("include") / name))
    shutil.copytree(stage, destination, symlinks=True)
    receipt = {"upstream_config_sha256": sha256(config_source), "copied_dependencies": []}
    shutil.copy2(config_source, destination / config_relative.with_suffix(".cmake.upstream"))
    (destination / config_relative).write_text(config)
    receipt["config_correction"] = "Remove unconditional optional libelf requirement from verified no-libelf Horizon build; no Mesa source or archive changed"
    for source, relative in copies:
        target = destination / relative
        shutil.copy2(source, target)
        receipt["copied_dependencies"].append({"source": str(source), "path": str(relative), "sha256": sha256(target)})
    receipt["files"] = [
        {"path": str(path.relative_to(destination)), "size": path.stat().st_size, "sha256": sha256(path)}
        for path in sorted(destination.rglob("*")) if path.is_file()
    ]
    (destination / "sdk-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"destination": str(destination), "file_count": len(receipt["files"]), "libvulkan_sha256": sha256(destination / "lib/libvulkan.a")}, indent=2))


if __name__ == "__main__":
    main()
