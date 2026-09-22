"""Archive entry ownership regression using the production mlib array hooks."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ArchiveMemoryTest(unittest.TestCase):
    def test_entry_lifetimes(self):
        root = Path(__file__).resolve().parents[2]
        header = (root / "applications/main/archive/helpers/archive_files.h").read_text()
        helpers = header[header.index("#define FAP_MANIFEST_MAX_ICON_SIZE"):header.index("#define M_OPL_ArchiveFile_t()")]
        arrays = header[header.index("#define M_OPL_ArchiveFile_t()"):header.index("void archive_set_file_type(")]
        harness = (Path(__file__).parent / "archive_memory.c").read_text()
        harness = harness.replace("/* HELPERS */", helpers).replace("/* ARRAYS */", arrays)
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            source.write_text(harness)
            flags = ["-fsanitize=address", "-g"] if os.environ.get("ARCHIVE_ASAN") else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS", *flags,
                            "-I", str(root / "lib/mlib"), str(source), "-o", str(binary)], check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
