"""Record linked sections, largest symbols, and stack configuration references.

Run after building. FAP allocatable sections (including code/rodata) use RAM;
firmware .rodata normally uses flash. This is not a runtime heap measurement.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--tools", type=Path, required=True, help="ARM binutils directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    def run(tool, *arguments):
        executable = args.tools / ("arm-none-eabi-" + tool)
        if executable.with_suffix(".exe").exists():
            executable = executable.with_suffix(".exe")
        return subprocess.check_output([str(executable), *arguments], text=True)

    sections = run("size", "-A", str(args.elf))
    symbols = run("nm", "-S", "--size-sort", "--radix=d", str(args.elf))
    stack_references = []
    pattern = re.compile(r"stack_size\s*=|furi_thread_(?:set_stack_size|alloc_ex)\s*\(")
    for directory in ("applications", "applications_user", "lib", "furi"):
        for path in sorted((root / directory).rglob("*")):
            if path.suffix not in (".c", ".h", ".fam"):
                continue
            for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                if pattern.search(line):
                    stack_references.append(f"{path.relative_to(root).as_posix()}:{number}: {line.strip()}")

    report = {
        "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "worktree_status": subprocess.check_output(["git", "status", "--short"], cwd=root, text=True),
        "elf": str(args.elf),
        "sections": sections,
        "largest_symbols_decimal": symbols.splitlines()[-40:][::-1],
        "largest_writable_symbols_decimal": [
            line for line in symbols.splitlines() if re.search(r"\s[bBdD]\s", line)
        ][-40:][::-1],
        "stack_configuration_references": stack_references,
        "runtime_measurements": "Pending hardware; references are not measured stack usage or a complete thread inventory.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
