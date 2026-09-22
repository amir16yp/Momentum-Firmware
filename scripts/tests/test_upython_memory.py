"""Host regressions for uPython using production REPL/file code and Furi strings."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class UPythonMemoryTest(unittest.TestCase):
    def test_history_editing_completion_and_file_cleanup(self):
        root = Path(__file__).resolve().parents[2]
        app = root / "applications/external/mp_flipper"
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        strings = (root / "furi/core/string.c").read_text().replace('#include "string.h"', '')
        repl = (app / "upython_repl.c").read_text().split("inline static bool continue_with_input")[0]
        file = (app / "upython_file.c").read_text()
        strip = lambda s: "\n".join(line for line in s.splitlines() if not line.startswith("#include"))
        harness = Path(__file__).with_name("upython_memory.c").read_text()
        source = harness.replace("/* REPL */", strip(repl)).replace("/* FILE */", strip(file))
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            test.write_text(source)
            string_source = Path(directory) / "strings.c"
            string_object = Path(directory) / "strings.o"
            # Match firmware M*LIB assertions; keep test assertions enabled.
            string_source.write_text(
                '#define NDEBUG\n#define _CRT_SECURE_NO_WARNINGS\n'
                '#define _CRT_NONSTDC_NO_WARNINGS\n'
                '#ifdef _WIN32\n#define strcasecmp _stricmp\n#endif\n'
                '#define _ATTRIBUTE(x) __attribute__(x)\n'
                '#include <stdlib.h>\n#include <furi/core/string.h>\n#include <m-string.h>\n'
                'void* tracked_alloc(size_t);\nvoid tracked_free(void*);\n'
                '#define malloc tracked_alloc\n#define free tracked_free\n' + strings)
            subprocess.run([compiler, "-std=gnu11", "-I", str(root),
                            "-I", str(root / "lib/mlib"), "-c", str(string_source),
                            "-o", str(string_object)], check=True)
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(root), "-I", str(root / "lib/mlib"),
                            str(test), str(string_object), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15, capture_output=True)


if __name__ == "__main__":
    unittest.main()
