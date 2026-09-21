"""Check production allocation glue with guarded host heap blocks."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class MemoryManagerTest(unittest.TestCase):
    def test_reallocation(self):
        root = Path(__file__).resolve().parents[2]
        heap = (root / "furi/core/memmgr_heap.c").read_text()
        glue = (root / "furi/core/memmgr.c").read_text()
        block = heap[heap.index("typedef struct A_BLOCK_LINK {"):heap.index("} BlockLink_t;") + len("} BlockLink_t;")]
        reallocate = heap[heap.index("void* pvPortRealloc("):heap.index("size_t xPortGetFreeHeapSize(")]
        wrapper = glue[glue.index("void* realloc("):glue.index("void* calloc(")]
        wrapper = wrapper.replace("void* realloc(", "void* firmware_realloc(")
        harness = (Path(__file__).parent / "memmgr_memory.c").read_text()
        harness = harness.replace("/* BLOCK_TYPE */", block).replace("/* REALLOC */", reallocate + wrapper)
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "Install clang or gcc to run this regression")
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            test.write_text(harness)
            flags = ["-fsanitize=address", "-g"] if os.environ.get("MEMMGR_ASAN") else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                            str(test), "-o", str(binary)], check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
