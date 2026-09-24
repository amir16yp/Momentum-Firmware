"""Compare Cortex-M4 helper code sizes against a revision (default: HEAD).

Uses Clang's freestanding ARM backend, not the firmware linker or target GCC.
The check stub traps on failure; these are directional object-size measurements.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def strip_includes(source):
    return "\n".join(line for line in source.splitlines()
                     if not line.startswith("#include") and line != "#pragma once")


def main():
    revision = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    compiler = shutil.which("clang")
    nm = shutil.which("llvm-nm")
    if not compiler or not nm:
        raise SystemExit("Install clang and llvm-nm")
    preamble = """
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_check(x) do { if(!(x)) __builtin_trap(); } while(0)
#define furi_crash(...) __builtin_trap()
int printf(const char*, ...);
"""
    symbols = ["bit_lib_set_bits", "bit_lib_get_bits", "bit_lib_reverse_16_fast",
               "bit_lib_crc8", "bit_lib_crc16", "iso13239_crc_calculate"]
    measurements = {}
    with tempfile.TemporaryDirectory() as directory:
        for version in ("before", "after"):
            def read(path):
                if version == "before":
                    return subprocess.check_output(["git", "show", f"{revision}:{path}"], cwd=ROOT, text=True)
                return (ROOT / path).read_text()
            code = preamble + strip_includes(read("lib/bit_lib/bit_lib.h"))
            code += strip_includes(read("lib/bit_lib/bit_lib.c"))
            code += "\ntypedef enum { Iso13239CrcTypeDefault, Iso13239CrcTypePicopass } Iso13239CrcType;\n"
            crc = strip_includes(read("lib/nfc/helpers/iso13239_crc.c"))
            code += crc[:crc.index("void iso13239_crc_append")].replace("static uint16_t", "uint16_t")
            source = Path(directory) / f"{version}.c"
            obj = source.with_suffix(".o")
            source.write_text(code)
            subprocess.run([compiler, "--target=arm-none-eabi", "-mcpu=cortex-m4", "-mthumb",
                            "-ffreestanding", "-Os", "-ffunction-sections", "-c", str(source),
                            "-o", str(obj)], check=True)
            output = subprocess.check_output([nm, "-S", "--radix=d", str(obj)], text=True)
            measurements[version] = {parts[-1]: int(parts[1]) for line in output.splitlines()
                                     if len(parts := line.split()) == 4 and parts[-1] in symbols}
    print("Function: before -> after (bytes)")
    for symbol in symbols:
        print(f"{symbol}: {measurements['before'][symbol]} -> {measurements['after'][symbol]}")


if __name__ == "__main__":
    main()
