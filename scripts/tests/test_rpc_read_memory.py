"""Exercise RPC file reads using the real nanopb codec and generated messages."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class RpcReadMemoryTest(unittest.TestCase):
    def test_streaming_and_cleanup(self):
        root = Path(__file__).resolve().parents[2]
        generated = root / "build/f7-firmware-C/assets/compiled"
        self.assertTrue((generated / "flipper.pb.h").exists(), "Build firmware_all first")
        source = (root / "applications/services/rpc/rpc_storage.c").read_text()
        read = source[source.index("static void rpc_system_storage_read_process("):source.index("static void rpc_system_storage_write_process(")]
        harness = (Path(__file__).parent / "rpc_read_memory.c").read_text()
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test = directory / "test.c"
            binary = directory / "test.exe"
            tracker = directory / "tracker.h"
            tracker.write_text("#include <stddef.h>\nvoid* tracked_realloc(void*, size_t);\nvoid tracked_free(void*);\n")
            test.write_text(harness.replace("/* PRODUCTION_CODE */", read))
            nanopb = root / "lib/nanopb"
            flags = ["-fsanitize=address", "-g"] if os.environ.get("RPC_READ_ASAN") else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                            "-DPB_ENABLE_MALLOC", "-Dpb_realloc=tracked_realloc", "-Dpb_free=tracked_free",
                            "-include", str(tracker), "-I", str(nanopb), "-I", str(generated),
                            str(test), *map(str, generated.glob("*.pb.c")),
                            str(nanopb / "pb_common.c"), str(nanopb / "pb_encode.c"),
                            str(nanopb / "pb_decode.c"), "-o", str(binary)], check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
