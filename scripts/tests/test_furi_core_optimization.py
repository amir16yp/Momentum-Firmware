"""Exercise production Furi code with host RTOS stubs and real M*LIB containers."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FuriCoreOptimizationTest(unittest.TestCase):
    def test_records_strings_and_logging(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "Install clang or gcc to run this regression")
        sources = []
        for name in ("string", "record", "log"):
            source = (root / "furi/core" / (name + ".c")).read_text()
            source = "\n".join(line for line in source.splitlines()
                               if not line.startswith('#include "') and
                               line != "#include <furi_hal.h>")
            if name == "string":
                source = source.replace("malloc(", "object_alloc(")
            sources.append(source)
        harness = Path(__file__).with_name("furi_core_optimization.c").read_text()
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            test.write_text(harness.replace("/* PRODUCTION_CODE */", "\n".join(sources)))
            flags = ["-fsanitize=address", "-g"] if os.environ.get("FURI_CORE_ASAN") else []
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", *flags,
                            "-I", str(root), "-I", str(root / "lib"),
                            "-I", str(root / "lib/mlib"), str(test), "-o", str(binary)],
                           check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
