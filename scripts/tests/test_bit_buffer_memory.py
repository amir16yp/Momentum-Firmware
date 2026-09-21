"""Host regression for production BitBuffer ownership and boundary handling."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class BitBufferMemoryTest(unittest.TestCase):
    def test_boundaries_and_lifetime(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "Install clang or gcc to run this regression")
        source = (root / "lib/toolbox/bit_buffer.c").read_text()
        source = source.replace("#include <furi.h>", "")
        harness = (Path(__file__).parent / "bit_buffer_memory.c").read_text()
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            test.write_text(harness.replace("/* PRODUCTION_CODE */", source))
            flags = ["-fsanitize=address", "-g"] if os.environ.get("BIT_BUFFER_ASAN") else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                            "-I", str(root / "lib/toolbox"), str(test), "-o", str(binary)],
                           check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
