"""Run the production Archive menu input branch without waiting for a GUI draw."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def run_menu_test(production):
    start = production.index("    if(in_menu) {", production.index("static bool archive_view_input("))
    end = production.index("    } else {\n        ArchiveFile_t* selected", start)
    # Compile the complete menu branch; the other branch handles file navigation.
    branch = production[start:end] + "    }\n    return true;\n"
    header = (ROOT / "applications/main/archive/views/archive_browser_view.h").read_text()
    start = header.index("typedef enum {\n    ArchiveBrowserEventFileMenuNone")
    events = header[start:header.index("} ArchiveBrowserEvent;", start) + len("} ArchiveBrowserEvent;")]
    harness = (Path(__file__).parent / "archive_menu.c").read_text()
    source = harness.replace("/* EVENTS */", events).replace("/* MENU_INPUT */", branch)
    compiler = shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Install clang or gcc to run the Archive menu regression")
    with tempfile.TemporaryDirectory() as directory:
        test = Path(directory) / "test.c"
        binary = Path(directory) / "test.exe"
        test.write_text(source)
        subprocess.run([
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS",
            "-I", str(ROOT / "lib/mlib"), str(test), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


class ArchiveMenuTest(unittest.TestCase):
    def test_input_before_redraw(self):
        production = (ROOT / "applications/main/archive/views/archive_browser_view.c").read_text()
        run_menu_test(production)


if __name__ == "__main__":
    unittest.main()
