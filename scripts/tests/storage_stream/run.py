"""Compile the production INA configuration loader against a host storage adapter.

Run from any directory: python scripts/tests/storage_stream/run.py
Requires a host clang compiler (or set CC).
"""
import os
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
root = here.parents[2]
source = root / "applications/external/ina_meter"
output = root / "build/storage-stream-tests"
output.mkdir(parents=True, exist_ok=True)
executable = output / ("config_test.exe" if os.name == "nt" else "config_test")
subprocess.run(
    [os.environ.get("CC", "clang"), "-std=c11", "-g",
     "-I", str(here), "-I", str(source),
     str(here / "config_test.c"), str(source / "app_config.c"), str(source / "slice.c"),
     "-o", str(executable)],
    check=True,
)
subprocess.run([str(executable)], check=True)
