"""Host regressions for the non-submodule library audit.

Set HOT_PATH_ASAN=1 to enable AddressSanitizer through the shared host runner.
"""
from pathlib import Path
import unittest

from test_hot_path_optimization import run_c

ROOT = Path(__file__).resolve().parents[2]


def production(path):
    return "\n".join(line for line in (ROOT / path).read_text().splitlines()
                     if not line.startswith("#include") and line != "#pragma once")


PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define furi_check(x) assert(x)
#define furi_assert(x) assert(x)
#define furi_crash(...) abort()
'''


class LibAuditTest(unittest.TestCase):
    def test_bits_and_crc(self):
        code = PREAMBLE + production("lib/bit_lib/bit_lib.h")
        code += production("lib/bit_lib/bit_lib.c")
        crc = production("lib/nfc/helpers/iso13239_crc.c")
        code += "\ntypedef enum { Iso13239CrcTypeDefault, Iso13239CrcTypePicopass } Iso13239CrcType;\n"
        code += crc[:crc.index("void iso13239_crc_append")]
        code += (Path(__file__).parent / "lib_audit_bits.c").read_text()
        print(run_c(code), end="")

    def test_varints(self):
        code = PREAMBLE + production("lib/toolbox/varint.h")
        code += production("lib/toolbox/varint.c")
        code += production("lib/lfrfid/tools/varint_pair.h")
        code += production("lib/lfrfid/tools/varint_pair.c")
        code += (Path(__file__).parent / "lib_audit_varint.c").read_text()
        print(run_c(code), end="")

    def test_array_and_pool(self):
        code = PREAMBLE + production("lib/toolbox/simple_array.h")
        harness = (Path(__file__).parent / "lib_audit_containers.c").read_text()
        code += harness.replace("/* ARRAY */", production("lib/toolbox/simple_array.c")).replace(
            "/* POOL */", production("lib/toolbox/buffer_stream.h") + production("lib/toolbox/buffer_stream.c"))
        print(run_c(code), end="")

    def test_infrared_decoder_ownership(self):
        source = production("lib/infrared/encoder_decoder/infrared.c")
        functions = source[source.index("InfraredDecoderHandler* infrared_alloc_decoder("):
                           source.index("const InfraredMessage* infrared_check_decoder_ready(")]
        code = PREAMBLE + r'''
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
typedef struct { void** ctx; } InfraredDecoderHandler;
static size_t allocations, releases, resets;
static void* tracked_malloc(size_t size) { ++allocations; return calloc(1, size); }
static void tracked_free(void* p) { ++releases; free(p); }
static void* decoder_alloc(void) { return tracked_malloc(4); }
static void decoder_reset(void* p) { assert(p); ++resets; }
static const struct {
    struct { void* (*alloc)(void); void (*free)(void*); void (*reset)(void*); } decoder;
} infrared_encoder_decoder[] = {
    {{decoder_alloc, tracked_free, decoder_reset}},
    {{NULL, NULL, NULL}},
    {{decoder_alloc, tracked_free, decoder_reset}},
};
void infrared_reset_decoder(InfraredDecoderHandler* handler);
''' + functions.replace("malloc(", "tracked_malloc(").replace("\n    free(", "\n    tracked_free(") + r'''
int main(void) {
    for(size_t i = 0; i < 100; ++i) {
        size_t before = allocations;
        InfraredDecoderHandler* handler = infrared_alloc_decoder();
        assert(allocations == before + 3);
        assert(handler->ctx[0] && !handler->ctx[1] && handler->ctx[2]);
        infrared_free_decoder(handler);
        assert(allocations == releases);
    }
    assert(resets == 200);
    return 0;
}
'''
        run_c(code)

    def test_input_wrappers(self):
        source = production("lib/print/wrappers.c")
        functions = source[source.index("int __wrap_fgetc("):source.index("int __wrap_ungetc(")]
        run_c(PREAMBLE + r'''
#define FuriWaitForever 0
static unsigned value, reads;
static size_t furi_thread_stdin_read(char* data, size_t size, int timeout) {
    (void)timeout; assert(size == 1); ++reads;
    *data = (char)value;
    return value < 256;
}
''' + functions + r'''
int main(void) {
    for(value = 0; value < 256; ++value) assert(__wrap_fgetc(stdin) == (int)value);
    assert(__wrap_fgetc(stdin) == EOF);
    char data[] = {1, 2};
    unsigned before = reads;
    assert(__wrap_fgets(data, 1, stdin) == data);
    assert(data[0] == 0 && data[1] == 2 && reads == before);
    value = 'a';
    assert(__wrap_fgets(data, 2, stdin) == data + 1);
    assert(data[0] == 'a' && data[1] == 0 && reads == before + 1);
    return 0;
}
''')

    def test_asset_boundaries(self):
        source = production("lib/momentum/asset_packs.c")
        source = source[source.index("typedef struct {"):source.index("static const char* const font_names")]
        harness = (Path(__file__).parent / "lib_audit_assets.c").read_text()
        run_c(PREAMBLE + harness.replace("/* PRODUCTION */", source))


if __name__ == "__main__":
    unittest.main()
