"""Host boundary regressions for production mJS/native-JS functions."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def run_harness(name, production):
    compiler = shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Install clang or gcc")
    harness = (Path(__file__).parent / name).read_text().replace("/* PRODUCTION_CODE */", production)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "test.c"
        binary = Path(directory) / "test.exe"
        source.write_text(harness)
        flags = ["-fsanitize=address", "-g"] if os.environ.get("JS_NATIVE_ASAN") else []
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                        str(source), "-o", str(binary)], check=True)
        env = os.environ.copy()
        if flags and os.name == "nt":
            resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
            env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
        subprocess.run([str(binary)], env=env, check=True)


class JsNativeMemoryTest(unittest.TestCase):
    def test_event_callback_retention(self):
        source = (ROOT / "applications/system/js_app/modules/js_event_loop/js_event_loop.c").read_text()
        functions = source[source.index("static void js_event_loop_callback_generic("):source.index("/**\n * @brief Cancels an event subscription")]
        run_harness("js_callback_memory.c", functions)

    def test_source_released_before_execution(self):
        source = (ROOT / "lib/mjs/mjs_exec.c").read_text()
        functions = source[source.index("static mjs_err_t mjs_exec_parsed("):source.index("mjs_err_t\n    mjs_call(")]
        run_harness("mjs_source_memory.c", functions)

    def test_typed_array_indices(self):
        source = (ROOT / "lib/mjs/mjs_array_buf.c").read_text()
        header = (ROOT / "lib/mjs/mjs_array_buf_public.h").read_text()
        types = header[header.index("typedef enum {"):header.index("} mjs_dataview_type_t;") + len("} mjs_dataview_type_t;")]
        functions = source[source.index("static size_t mjs_dataview_get_element_len("):source.index("mjs_val_t mjs_dataview_get_buf(")]
        run_harness("mjs_array_bounds.c", types + functions)

    def test_i2c_read_lengths(self):
        source = (ROOT / "applications/system/js_app/modules/js_i2c.c").read_text()
        helpers = source[source.index("static void ret_bad_args("):source.index("static void js_i2c_is_device_ready(")]
        reads = source[source.index("static void js_i2c_read("):source.index("static void* js_i2c_create(")]
        run_harness("js_i2c_memory.c", helpers + reads)


if __name__ == "__main__":
    unittest.main()
