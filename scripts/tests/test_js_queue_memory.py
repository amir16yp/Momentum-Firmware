"""Check JS queue ownership with mock RTOS queues and an explicit GC root table."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class JsQueueMemoryTest(unittest.TestCase):
    def test_queue_ownership(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "applications/system/js_app/modules/js_event_loop/js_event_loop.c").read_text()
        start = source.index("static mjs_val_t\n    js_event_loop_queue_transformer(")
        end = source.index("/**\n * @brief Creates a queue", start)
        functions = source[start:end]
        harness = (Path(__file__).parent / "js_queue_memory.c").read_text()
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            test.write_text(harness.replace("/* PRODUCTION_CODE */", functions))
            flags = ["-fsanitize=address", "-g"] if os.environ.get("JS_QUEUE_ASAN") else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                            str(test), "-o", str(binary)], check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
