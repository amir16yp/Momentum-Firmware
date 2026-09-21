"""Host regression for traversal path reuse; storage calls complete synchronously."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class RecursiveRemoveTest(unittest.TestCase):
    def test_path_reuse(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "applications/services/storage/storage_external_api.c").read_text()
        function = source[source.index("bool storage_simply_remove_recursive("):source.index("bool storage_simply_remove(Storage*")]
        harness = (Path(__file__).parent / "recursive_remove_memory.c").read_text()
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "Install clang or gcc to run this regression")
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "remove.c"
            binary = Path(directory) / "remove.exe"
            test.write_text(harness.replace("/* PRODUCTION_FUNCTION */", function))
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
