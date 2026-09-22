"""Host regressions for production string ownership and EMV file boundaries."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LibMemoryTest(unittest.TestCase):
    def test_lifetime_and_file_boundaries(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "Install clang or gcc to run this regression")
        strings = (root / "lib/toolbox/str_buffer.c").read_text().replace(
            '#include "str_buffer.h"', "")
        emv = (root / "lib/nfc/protocols/emv/emv.c").read_text()
        emv = emv[emv.index("bool emv_load("):emv.index("bool emv_is_equal(")]
        header = (root / "lib/nfc/protocols/emv/emv.h").read_text().replace(
            "#include <lib/nfc/protocols/iso14443_4a/iso14443_4a.h>", "").replace(
            "#pragma once", "")
        harness = (Path(__file__).parent / "lib_memory.c").read_text()
        source = harness.replace("/* EMV_HEADER */", header).replace(
            "/* PRODUCTION_CODE */", strings + emv)
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            test.write_text(source)
            flags = ["-fsanitize=address", "-g"] if os.environ.get("LIB_MEMORY_ASAN") else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            *flags, str(test), "-o", str(binary)], check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
