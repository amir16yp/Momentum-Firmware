"""Exercise production dictionary parsing with short reads and tracked strings."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class KeysDictionaryMemoryTest(unittest.TestCase):
    def test_parsing_and_memory(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "lib/toolbox/keys_dict.c").read_text()
        stream = (root / "lib/toolbox/stream/stream.c").read_text()
        args = (root / "lib/toolbox/args.c").read_text()
        hex_source = (root / "lib/toolbox/hex.c").read_text()
        types = source[source.index("struct KeysDict {"):source.index("static inline void keys_dict_add_ending_new_line(")]
        reader = source[source.index("static bool keys_dict_read_key_line("):source.index("bool keys_dict_check_presence(")]
        convert = source[source.index("static void keys_dict_str_to_int("):source.index("size_t keys_dict_get_total_keys(")]
        next_key = source[source.index("static bool keys_dict_get_next_key_str("):source.index("static bool keys_dict_is_key_present_str(")]
        read_line = stream[stream.index("bool stream_read_line("):stream.index("bool stream_rewind(")]
        parse_hex = hex_source[hex_source.index("bool hex_char_to_hex_nibble("):hex_source.index("bool hex_char_to_uint8(")]
        parse_hex += args[args.index("bool args_char_to_hex("):args.index("bool args_read_hex_bytes(")]
        harness = (Path(__file__).parent / "keys_dict_memory.c").read_text()
        harness = harness.replace("/* PRODUCTION_CODE */", parse_hex + read_line + types + reader + convert + next_key)
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler, "Install clang or gcc to run this regression")
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test.exe"
            test.write_text(harness)
            flags = ["-fsanitize=address", "-g"] if os.environ.get("KEYS_DICT_ASAN") else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                            str(test), "-o", str(binary)], check=True)
            env = os.environ.copy()
            if flags and os.name == "nt":
                resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
                env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
            subprocess.run([str(binary)], env=env, check=True)


if __name__ == "__main__":
    unittest.main()
