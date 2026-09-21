"""Exercise production extraction with microtar, uzlib and a mock output disk."""
import gzip
import io
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


class TarExtractionTest(unittest.TestCase):
    def test_gzip_extraction(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "lib/toolbox/tar/tar_archive.c").read_text()
        seek = source[source.index("static int mtar_compressed_file_seek("):source.index("const struct mtar_ops compressed_ops")]
        start = source.index("typedef struct {\n    TarArchive* archive;")
        extraction = source[start:source.index("bool tar_archive_add_file(", start)]
        single = source[source.index("bool tar_archive_unpack_file("):]
        harness = (Path(__file__).parent / "tar_extraction.c").read_text()
        harness = harness.replace("/* SEEK */", seek).replace("/* EXTRACTION */", extraction + single)
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            test = folder / "test.c"
            binary = folder / "test.exe"
            test.write_text(harness)
            microtar = root / "lib/microtar/src"
            uzlib = root / "lib/uzlib/src"
            subprocess.run([compiler, "-std=c11", "-O1", "-I", str(microtar), "-I", str(uzlib),
                            str(test), str(microtar / "microtar.c"),
                            *map(str, uzlib.glob("*.c")), "-o", str(binary)], check=True)
            tar = io.BytesIO()
            with tarfile.open(fileobj=tar, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                for name, size in [("dir", None), ("empty", 0), ("one", 1),
                                   ("skip", 23001), ("dir/edge", 511),
                                   ("sector", 512), ("over", 513), ("large", 25001)]:
                    header = tarfile.TarInfo(name)
                    if size is None:
                        header.type = tarfile.DIRTYPE
                        archive.addfile(header)
                    else:
                        header.size = size
                        archive.addfile(header, io.BytesIO(bytes(i % 251 for i in range(size))))
            packed = gzip.compress(tar.getvalue(), mtime=0)
            fixture = folder / "fixture.gz"
            fixture.write_bytes(packed)
            subprocess.run([str(binary), str(fixture)], check=True)


if __name__ == "__main__":
    unittest.main()
