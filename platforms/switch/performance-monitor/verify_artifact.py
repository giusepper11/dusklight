#!/usr/bin/env python3
"""Read-only package/link checks, then exclusive local artifact+manifest creation.

No device discovery, transfer, hardware execution, or publication is implemented.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


SDK_VULKAN_SHA256 = "4ef2012e1952dcad0dfd4e916d15535e36aaeca20214de7adfc4a8aa734b7e30"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def romfs_files(data):
    require(len(data) >= 0x50, "ROMFS header truncated")
    header, dh, dhs, dm, dms, fh, fhs, fm, fms, fd = struct.unpack_from("<10Q", data)
    require(header == 0x50, "ROMFS header size is not 0x50")
    for offset, size in [(dh, dhs), (dm, dms), (fh, fhs), (fm, fms)]:
        require(offset >= header and offset + size <= len(data), "ROMFS table out of bounds")
    require(header <= fd <= len(data),
            "ROMFS file-data offset out of bounds")
    files = {}
    visited_dirs, visited_files = set(), set()

    def name_at(start, size, limit):
        require(size <= limit - start, "ROMFS name out of bounds")
        name = bytes(data[start:start + size]).decode("utf-8")
        require("/" not in name and "\\" not in name and name not in (".", ".."), "Unsafe ROMFS name")
        return name

    def visit_directory(offset, prefix):
        require(offset not in visited_dirs and offset + 24 <= dms, "ROMFS directory cycle or invalid entry")
        visited_dirs.add(offset)
        entry = dm + offset
        _, sibling, child_dir, child_file, _, length = struct.unpack_from("<6I", data, entry)
        name = name_at(entry + 24, length, dm + dms)
        path = f"{prefix}/{name}" if prefix and name else name or prefix
        next_file = child_file
        while next_file != 0xffffffff:
            require(next_file not in visited_files and next_file + 32 <= fms,
                    "ROMFS file cycle or invalid entry")
            visited_files.add(next_file)
            file_entry = fm + next_file
            _, next_file, data_offset, size, _, file_name_size = struct.unpack_from("<IIQQII", data, file_entry)
            file_name = name_at(file_entry + 32, file_name_size, fm + fms)
            require(fd + data_offset + size <= len(data), "ROMFS file out of bounds")
            for table, table_size in [(dh, dhs), (dm, dms), (fh, fhs), (fm, fms)]:
                require(size == 0 or fd + data_offset + size <= table or fd + data_offset >= table + table_size,
                        "ROMFS file overlaps metadata table")
            full_name = f"{path}/{file_name}" if path else file_name
            require(full_name not in files, "Duplicate ROMFS path")
            files[full_name] = bytes(data[fd + data_offset:fd + data_offset + size])
        next_dir = child_dir
        while next_dir != 0xffffffff:
            next_dir = visit_directory(next_dir, path)
        return sibling

    visit_directory(0, "")
    return files


def inspect_nro(data):
    require(len(data) >= 0x80 and data[0x10:0x14] == b"NRO0", "Missing NRO0 header")
    nro_size = struct.unpack_from("<I", data, 0x18)[0]
    require(nro_size >= 0x80 and nro_size + 0x38 <= len(data), "NRO extent out of bounds")
    require(data[nro_size:nro_size + 4] == b"ASET", "Missing ASET header")
    sections = {}
    for index, name in enumerate(["icon", "nacp", "romfs"]):
        offset, size = struct.unpack_from("<QQ", data, nro_size + 8 + index * 16)
        require(offset >= 0x38 and size > 0 and nro_size + offset + size <= len(data),
                f"{name} assets out of bounds")
        sections[name] = bytes(data[nro_size + offset:nro_size + offset + size])
    require(len(sections["nacp"]) == 0x4000, "NACP is not 16 KiB")
    title = sections["nacp"][:0x200].split(b"\0", 1)[0].decode("utf-8")
    require(title == "Dusklight", "Unexpected NACP title")
    files = romfs_files(sections["romfs"])
    return {"nro_size_without_assets": nro_size, "nacp_title": title, "nacp_size": 0x4000,
            "romfs_size": len(sections["romfs"]), "romfs_file_count": len(files),
            "icon_sha256": sha256(sections["icon"]), "nacp_sha256": sha256(sections["nacp"]),
            "romfs_sha256": sha256(sections["romfs"])}, files


def inspect_elf(data):
    require(len(data) >= 64 and data[:4] == b"\x7fELF" and data[4:6] == b"\x02\x01",
            "ELF must be 64-bit little endian")
    elf_type, machine = struct.unpack_from("<HH", data, 16)
    require(machine == 183 and elf_type == 3, "ELF must be AArch64 PIE")
    return {"architecture": "AArch64", "elf_type": "PIE", "sha256": sha256(data)}


def require_clean_source(source):
    status = subprocess.check_output(
        ["git", "-C", str(source), "status", "--porcelain=v1", "--untracked-files=all",
         "--ignore-submodules=none"], text=True)
    require(not status, f"Source has tracked, staged, submodule or untracked changes: {source}\n{status}")


def require_matching_package(nro, regenerated):
    require(nro == regenerated, "NRO is not byte-identical to independent packaging of the verified ELF")


def verify_nro_matches_elf(nro, elf, package_directory, icon, elf2nro, output):
    rebuilt = output / "validation-rebuilt.nro"
    require(not rebuilt.exists(), "Regeneration output must be new")
    command = [str(elf2nro), str(elf), str(rebuilt),
               f"--nacp={package_directory / 'dusklight.nacp'}", f"--icon={icon}",
               f"--romfsdir={package_directory / 'romfs'}"]
    subprocess.run(command, check=True)
    require_matching_package(nro, rebuilt.read_bytes())
    return {"method": "independent elf2nro regeneration from verified ELF and original packaging inputs",
            "byte_identical": True, "sha256": sha256(nro), "command": command}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["nro", "elf", "identity", "sdk-root", "output-dir", "nm", "elf2nro", "toolchain-image"]:
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    output = Path(args.output_dir).resolve()
    require(output.is_relative_to(root / "build"), "Output must be inside this feature worktree's build directory")
    lock = Path(args.identity).resolve().parent / ".performance-monitor-packaged.json"
    require(lock.parent.is_relative_to(root / "build") and not lock.exists(), "Build configuration is already sealed or outside feature build root")
    identity = json.loads(Path(args.identity).read_text())
    require(identity["source_dirty_at_configure"] is False, "Configure from committed clean feature sources first")
    require(identity["toolchain_image"] == args.toolchain_image, "Toolchain identity mismatch")
    for source, key in [(root, "game_source_revision"), (root / "extern/aurora", "aurora_source_revision")]:
        head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        require(head == identity[key], f"Compiled {key} is not current source HEAD")
        require_clean_source(source)
    nro = Path(args.nro).read_bytes()
    elf = Path(args.elf).read_bytes()
    package, files = inspect_nro(nro)
    elf_report = inspect_elf(elf)
    require(identity["build_id"].encode() in elf, "Compiled build ID not found in ELF")
    require(identity["build_id"].encode() in nro, "Compiled build ID not found in NRO executable")
    for label in [b"Performance Monitor", b"Start Benchmark", b"Stop Benchmark", b"Export Results"]:
        require(label in elf, f"Missing compiled UI label: {label!r}")
    symbols = subprocess.check_output([args.nm, "-g", args.elf], text=True)
    lines = [line.split() for line in symbols.splitlines() if line.split()]
    require(not any(parts[0] == "U" for parts in lines), "ELF has strong undefined symbols")
    defined = {parts[-1] for parts in lines if len(parts) >= 3}
    for symbol in ["_start", "aurora_set_present_callback", "vkGetInstanceProcAddr", "vkCreateViSurfaceNN",
                   "__wrap_vk_icdGetInstanceProcAddr", "__wrap_posix_memalign"]:
        require(symbol in defined, f"Missing linked symbol {symbol}")
    require(not any(name.startswith("__wrap_nvk_") for name in defined), "Stale private NVK dispatch wrapper")
    expected = {"res/" + str(path.relative_to(root / "res")).replace("\\", "/"): sha256(path.read_bytes())
                for path in (root / "res").rglob("*") if path.is_file()}
    require(set(files) == set(expected), "Packaged ROMFS file inventory differs from source res/")
    for name, expected_hash in expected.items():
        require(sha256(files[name]) == expected_hash, f"Packaged resource differs: {name}")
    require(b"fps[detailed]" in files["res/rml/overlay.rcss"], "Detailed panel stylesheet absent")
    sdk = Path(args.sdk_root).resolve()
    receipt_path = sdk / "sdk-receipt.json"
    receipt = json.loads(receipt_path.read_text())
    for item in receipt["files"]:
        path = (sdk / item["path"]).resolve()
        require(path.is_relative_to(sdk), "SDK receipt path escaped SDK root")
        require(sha256(path.read_bytes()) == item["sha256"], f"SDK receipt mismatch: {item['path']}")
    require(sha256((sdk / "lib/libvulkan.a").read_bytes()) == SDK_VULKAN_SHA256, "Not the preserved upstream NVK SDK")
    output.mkdir(parents=True, exist_ok=False)
    binding = verify_nro_matches_elf(nro, Path(args.elf), Path(args.nro).parent,
                                   root / "platforms/switch/icon.jpg", Path(args.elf2nro), output)
    manifest = {**identity, "nro_sha256": sha256(nro), "nro_size_bytes": len(nro),
                "artifact_filename": f"dusklight-performance-monitor-{identity['game_source_revision'][:12]}-{identity['build_id'][:8]}.nro",
                "mesa_source_revision": "0ff52079ca9e9e837ed27eb95a8d239f439a1864",
                "nro_hash_origin": "host-verified completed build artifact", "hardware_execution": False}
    report = {"status": "HOST/BUILD PASS — HARDWARE UNVERIFIED", "package": package, "elf": elf_report,
              "elf_nro_binding": binding,
              "sdk_receipt_sha256": sha256(receipt_path.read_bytes()), "sdk_files_verified": len(receipt["files"]),
              "resources_verified": len(expected), "hardware_contact": False, "manifest": manifest}
    with (output / manifest["artifact_filename"]).open("xb") as file:
        file.write(nro)
    for filename, value in [("performance-monitor-build.json", manifest), ("verification.json", report),
                            ("resource-manifest.json", expected)]:
        with (output / filename).open("x") as file:
            json.dump(value, file, indent=2, ensure_ascii=False)
            file.write("\n")
    with (output / "elf-symbols.txt").open("x") as file:
        file.write(symbols)
    with lock.open("x") as file:
        json.dump({"build_id": identity["build_id"], "nro_sha256": sha256(nro),
                   "delivery_directory": str(output)}, file, indent=2)
        file.write("\n")
    print(json.dumps({"output_directory": str(output), **report}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
