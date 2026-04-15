#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
from collections import Counter
from pathlib import Path


def find_tool(name: str) -> Path | None:
    # Prefer the PlatformIO toolchain if installed in the local user packages.
    pio_toolchain = Path.home() / ".platformio" / "packages" / "toolchain-riscv32-esp" / "bin" / name
    if pio_toolchain.exists():
        return pio_toolchain
    which = shutil.which(name)
    if which:
        return Path(which)
    return None


def print_object_symbol_summary(obj_file: Path, nm_tool: Path, count: int = 50) -> None:
    print(f"\n=== Symbol breakdown for object: {obj_file.relative_to(Path.cwd())} ===")
    result = subprocess.run([str(nm_tool), "--size-sort", "--print-size", str(obj_file)], capture_output=True, text=True, check=True)
    symbols = []
    for line in result.stdout.splitlines():
        fields = line.strip().split(None, 3)
        if len(fields) < 4:
            continue
        size = int(fields[1], 16)
        name = fields[3]
        symbols.append((size, name))

    categories = Counter()
    for size, name in symbols:
        if "bookerly" in name:
            categories["bookerly"] += size
        elif "notosans" in name or "noto" in name:
            categories["notosans"] += size
        elif "ubuntu" in name:
            categories["ubuntu"] += size
        elif "lexend" in name:
            categories["lexend"] += size
        elif "Font" in name or "font" in name:
            categories["other_font"] += size
        else:
            categories["other"] += size

    print("\nSymbol categories:")
    for category, total in categories.most_common():
        print(f"  {category:12s} {total:10,d}")

    print(f"\nTop {count} symbols by size:")
    for size, name in sorted(symbols, reverse=True)[:count]:
        print(f"  {size:10,d}  {name}")


def module_name_for_path(relpath: str) -> str:
    parts = relpath.split(os.sep)
    if not parts:
        return relpath
    if parts[0] == "src":
        return "project/src"
    if parts[0] == "FrameworkArduino":
        return "framework/arduino"
    if parts[0].startswith("lib") and len(parts) > 1:
        return parts[1]
    return parts[0]


def print_section_summary(sections: dict[str, int], firmware_bin_size: int) -> None:
    print("\n=== Section flash/RAM summary ===")
    print(f"Firmware BIN size: {firmware_bin_size:,} bytes")
    flash_sections = [
        ".flash.text",
        ".flash.rodata",
        ".flash.rodata_noload",
        ".flash.appdesc",
        ".flash.tdata",
        ".flash.tbss",
    ]
    runtime_sections = [".dram0.data", ".dram0.bss", ".iram0.text", ".rtc.text", ".rtc_noinit"]

    print("\nFlash sections:")
    for name in flash_sections:
        if name in sections:
            print(f"  {name:18s} {sections[name]:10,d}")
    print("\nRuntime sections:")
    for name in runtime_sections:
        if name in sections:
            print(f"  {name:18s} {sections[name]:10,d}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a ROM usage report for this PlatformIO build.",
    )
    parser.add_argument(
        "--env",
        default="gh_release",
        help="PlatformIO environment to inspect (default: gh_release)",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Run `pio run -e <env>` before reporting if the build output is missing.",
    )
    parser.add_argument(
        "--object",
        default=None,
        help="Object file to analyze under the build directory, e.g. src/main.cpp.o.",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parent.parent
    build_dir = [d for d in Path.iterdir(project_root / ".pio" / "build") if d.is_dir()][0] #since it destroys old builds.
    elf_file = build_dir / "firmware.elf"
    bin_file = build_dir / "firmware.bin"
    size_tool = find_tool("riscv32-esp-elf-size")
    nm_tool = find_tool("riscv32-esp-elf-nm")

    if size_tool is None:
        print("ERROR: Could not locate riscv32-esp-elf-size. Install PlatformIO or add the toolchain bin to PATH.")
        return 1
    if args.object is not None and nm_tool is None:
        print("ERROR: Could not locate riscv32-esp-elf-nm. Install PlatformIO or add the toolchain bin to PATH.")
        return 1

    if not elf_file.exists():
        if args.build:
            print(f"Building environment '{args.env}'...")
            subprocess.run(["pio", "run", "-e", args.env], check=True, cwd=project_root)
        else:
            print(f"ERROR: Missing build output: {elf_file}")
            print("Run with --build or build the environment first.")
            return 1

    if not elf_file.exists():
        print(f"ERROR: Build output still missing: {elf_file}")
        return 1

    if not bin_file.exists():
        print(f"WARNING: firmware.bin is missing; flash size will be estimated from ELF sections.")

    result = subprocess.run([str(size_tool), "-A", str(elf_file)], capture_output=True, text=True, check=True)
    sections = {}
    for line in result.stdout.splitlines():
        parts = line.strip().split()
        if len(parts) == 3 and parts[0].startswith("."):
            try:
                sections[parts[0]] = int(parts[1])
            except ValueError:
                pass

    firmware_bin_size = bin_file.stat().st_size if bin_file.exists() else sum(
        sections.get(name, 0)
        for name in [".flash.text", ".flash.rodata", ".flash.rodata_noload", ".flash.appdesc", ".flash.tdata", ".flash.tbss"]
    )

    print("ROM Size Usage Report")
    print("=====================")
    print(f"Environment: {args.env}")
    print(f"Build dir: {build_dir}")
    print_section_summary(sections, firmware_bin_size)

    print("\n=== Top library/module flash contributors ===")
    module_sizes = Counter()
    object_sizes = []

    for obj in sorted(build_dir.rglob("*.o")):
        result = subprocess.run([str(size_tool), str(obj)], capture_output=True, text=True)
        if result.returncode != 0:
            continue
        line = result.stdout.strip().splitlines()[-1]
        fields = line.split()
        if len(fields) < 6:
            continue
        text = int(fields[0])
        data = int(fields[1])
        size = text + data
        relpath = obj.relative_to(build_dir).as_posix()
        module_sizes[module_name_for_path(relpath)] += size
        object_sizes.append((size, relpath))

    for module, sz in module_sizes.most_common(20):
        print(f"  {sz:10,d}  {module}")

    print("\n=== Top object files by flash size ===")
    for size, relpath in sorted(object_sizes, reverse=True)[:25]:
        print(f"  {size:10,d}  {relpath}")

    if args.object is not None:
        object_file = build_dir / args.object
        if object_file.exists():
            print_object_symbol_summary(object_file, nm_tool)
        else:
            print(f"\nWARNING: Requested object file does not exist: {object_file}")

    return 0


if __name__ == "__main__":
    import shutil

    raise SystemExit(main())
