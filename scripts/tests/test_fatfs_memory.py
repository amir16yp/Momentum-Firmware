"""Run virtual-volume lifetimes against the real FatFs engine on a RAM disk."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FatfsMemoryTest(unittest.TestCase):
    def test_virtual_volume(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "applications/services/storage/storages/storage_ext.c").read_text()
        lifetime = source[source.index("FS_Error storage_process_virtual_mount("):source.index("static DSTATUS mnt_driver_initialize(")]
        init = source[source.index("void storage_mnt_init("):]
        drivers = source[source.index("static DRESULT mnt_driver_read("):source.index("static DRESULT mnt_driver_ioctl(")]
        harness = (Path(__file__).parent / "fatfs_memory.c").read_text()
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test = directory / "test.c"
            binary = directory / "test.exe"
            # Use the firmware's fixed-width FatFs types, not Windows' UINT/DWORD.
            integer = directory / "embedded_integer.h"
            integer.write_text((root / "lib/fatfs/integer.h").read_text().replace("#ifdef _WIN32", "#if 0"))
            test.write_text(harness.replace("/* PRODUCTION_CODE */", lifetime + init + drivers))
            flags = ["-fsanitize=address", "-g"] if os.environ.get("FATFS_ASAN") else []
            subprocess.run([compiler, "-std=c11", *flags, "-include", str(integer),
                            "-I", str(root / "lib/fatfs"), "-I", str(root / "targets/f7/fatfs"),
                            str(test), str(root / "lib/fatfs/ff.c"),
                            str(root / "lib/fatfs/option/unicode.c"), "-o", str(binary)], check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
