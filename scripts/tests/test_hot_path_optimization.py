"""Host checks for rendering and stream allocation/operation counts.

Set OPTIMIZATION_BASELINE_REV to a git revision to measure the same cases before changes.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    revision = os.environ.get("OPTIMIZATION_BASELINE_REV")
    if revision:
        return subprocess.check_output(["git", "show", f"{revision}:{path}"], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def run_c(code):
    compiler = shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Install clang or gcc to run these checks")
    with tempfile.TemporaryDirectory() as directory:
        test = Path(directory) / "test.c"
        binary = Path(directory) / "test.exe"
        test.write_text(code)
        flags = ["-fsanitize=address", "-g"] if os.environ.get("HOT_PATH_ASAN") else []
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                        "-Wno-unused-function", str(test), "-o", str(binary)], check=True)
        env = os.environ.copy()
        if flags and os.name == "nt":
            resource = subprocess.check_output([compiler, "--print-resource-dir"], text=True).strip()
            env["PATH"] = str(Path(resource) / "lib/windows") + os.pathsep + env.get("PATH", "")
        return subprocess.check_output([str(binary)], env=env, text=True, timeout=15)


PREAMBLE = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#define furi_check(x) assert(x)
#define furi_crash() abort()
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
typedef struct { char data[8192]; } FuriString;
unsigned allocations, frees, copied;
static FuriString* furi_string_alloc(void) {
    ++allocations;
    return calloc(1, sizeof(FuriString));
}
static void furi_string_free(FuriString* s) { ++frees; free(s); }
static const char* furi_string_get_cstr(const FuriString* s) { return s->data; }
static void furi_string_set_strn(FuriString* s, const char* text, size_t n) {
    assert(n < sizeof(s->data));
    memcpy(s->data, text, n); s->data[n] = 0; copied += n;
}
static FuriString* furi_string_alloc_set(const char* text) {
    FuriString* s = furi_string_alloc();
    furi_string_set_strn(s, text, strlen(text)); return s;
}
static void furi_string_left(FuriString* s, size_t n) { s->data[n] = 0; }
static void furi_string_cat_str(FuriString* s, const char* text) { strcat(s->data, text); }
static void furi_string_printf(FuriString* s, const char* format, ...) {
    va_list ap; va_start(ap, format);
    vsnprintf(s->data, sizeof(s->data), format, ap); va_end(ap);
}
static FuriString* furi_string_alloc_printf(const char* format, ...) {
    FuriString* s = furi_string_alloc();
    va_list ap; va_start(ap, format);
    vsnprintf(s->data, sizeof(s->data), format, ap); va_end(ap); return s;
}
'''


class HotPathOptimizationTest(unittest.TestCase):
    def test_submenu_visible_items(self):
        code = source("applications/services/gui/modules/submenu.c")
        start = code.index("static size_t submenu_items_on_screen(")
        end = code.index("\nstatic bool submenu_view_input_callback", start)
        harness = PREAMBLE + r'''
typedef struct { FuriString* label; bool locked; FuriString* locked_message; } SubmenuItem;
typedef struct { SubmenuItem* data; size_t size; } ItemArray;
typedef struct { ItemArray items; FuriString* header; size_t position, window_position;
                 bool locked_message_visible, is_vertical; } SubmenuModel;
typedef struct { ItemArray array; size_t index; } Iterator;
typedef Iterator SubmenuItemArray_it_t[1];
static unsigned visits;
static size_t SubmenuItemArray_size(ItemArray a) { return a.size; }
static SubmenuItem* SubmenuItemArray_get(ItemArray a, size_t i) {
    assert(i < a.size); ++visits; return &a.data[i];
}
static void SubmenuItemArray_it(Iterator* it, ItemArray a) { it->array = a; it->index = 0; }
static bool SubmenuItemArray_end_p(Iterator* it) { return it->index == it->array.size; }
static void SubmenuItemArray_next(Iterator* it) { ++it->index; }
static SubmenuItem* SubmenuItemArray_cref(Iterator* it) { return SubmenuItemArray_get(it->array, it->index); }
static bool furi_string_empty(FuriString* s) { return !s->data[0]; }
static void furi_string_set(FuriString* s, FuriString* value) { strcpy(s->data, value->data); }
static FuriString* label_copy(FuriString* s) { return furi_string_alloc_set(s->data); }
typedef struct { size_t width, height; } Canvas;
enum { FontPrimary, FontSecondary, ColorBlack, ColorWhite, AlignCenter };
static const int I_Lock_7x8 = 1, I_WarningDolphin_45x42 = 2;
static size_t canvas_width(Canvas* c) { return c->width; }
static size_t canvas_height(Canvas* c) { return c->height; }
static void canvas_clear(Canvas* c) { (void)c; }
static void canvas_set_font(Canvas* c, int font) { (void)c; (void)font; }
static int color;
static void canvas_set_color(Canvas* c, int value) { (void)c; color = value; }
static SubmenuModel* expected;
static size_t drawn, locks, highlights, overlays;
static void canvas_draw_str(Canvas* c, int x, int y, const char* s) {
    (void)c;
    if(x == 4) { assert(y == 11 && !strcmp(s, "Header")); return; }
    size_t position = expected->window_position + drawn;
    assert(x == 6 && y == (int)(drawn * 16 + (expected->header->data[0] ? 16 : 0) + 12));
    assert((size_t)atoi(s) == position);
    assert(color == (position == expected->position ? ColorWhite : ColorBlack));
    ++drawn;
}
static void elements_slightly_rounded_box(Canvas* c, int x, int y, int w, int h) {
    (void)c; (void)x; (void)y; (void)w; assert(h == 14); ++highlights;
}
static void elements_string_fit_width(Canvas* c, FuriString* s, int width) {
    assert(width == (int)c->width - 5 - ((atoi(s->data) % 2) ? 21 : 11));
}
static void canvas_draw_icon(Canvas* c, int x, int y, const int* icon) {
    (void)c; (void)x; (void)y; if(icon == &I_Lock_7x8) ++locks;
}
static void elements_scrollbar(Canvas* c, size_t position, size_t size) {
    (void)c; assert(position == expected->position && size == expected->items.size);
}
static void canvas_draw_box(Canvas* c, int x, int y, int w, int h) {
    (void)c; (void)x; (void)y; (void)w; (void)h;
}
static void canvas_draw_rframe(Canvas* c, int x, int y, int w, int h, int radius) {
    (void)radius; canvas_draw_box(c, x, y, w, h);
}
static void elements_multiline_text_aligned(Canvas* c, int x, int y, int h, int v, const char* s) {
    (void)c; (void)x; (void)y; (void)h; (void)v; assert(!strcmp(s, "Locked")); ++overlays;
}
''' + code[start:end].replace("furi_string_alloc_set(", "label_copy(") + r'''
int main(void) {
    SubmenuItem items[1000];
    FuriString* header = furi_string_alloc_set("");
    FuriString* message = furi_string_alloc_set("Locked");
    for(size_t i = 0; i < 1000; ++i) {
        items[i].label = furi_string_alloc();
        furi_string_printf(items[i].label, "%zu", i);
        items[i].locked = i % 2; items[i].locked_message = message;
    }
    for(unsigned scenario = 0; scenario < 6; ++scenario) {
        SubmenuModel model = {.items = {items, 1000}, .header = header, .position = 501,
                              .window_position = 500, .is_vertical = scenario == 1,
                              .locked_message_visible = scenario == 5};
        strcpy(header->data, scenario == 1 ? "Header" : "");
        if(scenario == 2) model.window_position = model.position = 999;
        if(scenario == 3) model.items.size = model.window_position = model.position = 0;
        if(scenario == 4) model.window_position = 1001;
        Canvas canvas = {128, 64};
        expected = &model;
        drawn = locks = highlights = overlays = visits = 0;
        unsigned before = allocations, before_free = frees;
        submenu_view_draw_callback(&canvas, &model);
        assert(allocations - before == frees - before_free);
        size_t count = scenario == 1 ? 7 : scenario == 2 ? 1 : (scenario == 3 || scenario == 4) ? 0 : 4;
        assert(drawn == count && highlights == (count ? 1u : 0u));
        assert(locks == (scenario == 2 ? 1 : count / 2));
        assert(overlays == (scenario == 5));
        if(scenario == 0) printf("1000-item menu: item_accesses=%u allocations=%u\n", visits, allocations - before);
    }
    for(size_t i = 0; i < 1000; ++i) furi_string_free(items[i].label);
    furi_string_free(header); furi_string_free(message);
    assert(allocations == frees);
    return 0;
}
'''
        result = run_c(harness)
        if not os.environ.get("OPTIMIZATION_BASELINE_REV"):
            self.assertIn("item_accesses=4 allocations=1", result)
        print(result, end="")

    def test_string_stream_read(self):
        code = source("lib/toolbox/stream/string_stream.c")
        start = code.index("static size_t string_stream_read(StringStream* stream, char* data, size_t size) {")
        end = code.index("\nstatic bool string_stream_delete_and_insert", start)
        harness = PREAMBLE + r'''
typedef struct { FuriString* string; size_t index, length; } StringStream;
enum { StreamOffsetFromCurrent };
static unsigned queries, seeks;
static size_t string_stream_size(StringStream* s) { ++queries; return s->length; }
static bool string_stream_eof(StringStream* s) { return s->index >= string_stream_size(s); }
static bool string_stream_seek(StringStream* s, int32_t n, int origin) {
    assert(origin == StreamOffsetFromCurrent); ++seeks; s->index += n; return true;
}
''' + code[start:end] + r'''
int main(void) {
    FuriString* text = furi_string_alloc_set("abcdefghijklmnopqrstuvwxyz");
    StringStream s = {text, 0, 26};
    char out[32]; memset(out, 0x55, sizeof(out));
    assert(string_stream_read(&s, out, 20) == 20);
    assert(s.index == 20 && memcmp(out, text->data, 20) == 0 && out[20] == 0x55);
    printf("20-byte read: size_queries=%u seeks=%u\n", queries, seeks);
    assert(string_stream_read(&s, NULL, 0) == 0 && s.index == 20);
    assert(string_stream_read(&s, out, sizeof(out)) == 6);
    assert(s.index == 26 && memcmp(out, "uvwxyz", 6) == 0);
    assert(string_stream_read(&s, out, sizeof(out)) == 0 && s.index == 26);
    s.index = 0; text->data[3] = 0;
    assert(string_stream_read(&s, out, SIZE_MAX) == 26 && s.index == 26);
    assert(memcmp(out, text->data, 26) == 0);
    s.index = 0; s.length = 0; text->data[0] = 0;
    assert(string_stream_read(&s, out, 5) == 0 && s.index == 0);
    furi_string_free(text);
    return 0;
}
'''
        result = run_c(harness)
        if not os.environ.get("OPTIMIZATION_BASELINE_REV"):
            self.assertIn("size_queries=1 seeks=0", result)
        print(result, end="")

    def test_stream_copy(self):
        code = source("lib/toolbox/stream/stream.c")
        start = code.index("size_t stream_copy(")
        end = code.index("\nsize_t stream_copy_full", start)
        harness = PREAMBLE + r'''
#define STREAM_CACHE_SIZE 512u
typedef struct { uint8_t data[2048]; size_t size, pos, limit; } Stream;
static unsigned buffer_allocations;
static size_t buffer_size;
static void* buffer_alloc(size_t n) { ++buffer_allocations; buffer_size = n; return malloc(n); }
static size_t stream_read(Stream* s, uint8_t* data, size_t n) {
    n = MIN(n, s->size - s->pos); n = MIN(n, s->limit);
    memcpy(data, s->data + s->pos, n); s->pos += n; return n;
}
static size_t stream_write(Stream* s, const uint8_t* data, size_t n) {
    n = MIN(n, s->limit); assert(s->pos + n <= sizeof(s->data));
    memcpy(s->data + s->pos, data, n); s->pos += n; return n;
}
''' + code[start:end].replace("malloc(", "buffer_alloc(") + r'''
int main(void) {
    Stream from = {.size = 2048, .limit = 2048}, to = {.limit = 2048};
    for(size_t i = 0; i < sizeof(from.data); ++i) from.data[i] = (uint8_t)i;
    assert(stream_copy(&from, &to, 0) == 0 && from.pos == 0 && to.pos == 0);
    printf("empty copy: allocations=%u\n", buffer_allocations);
    assert(stream_copy(&from, &to, 13) == 13);
    printf("13-byte copy: allocated_bytes=%zu\n", buffer_size);
    assert(memcmp(from.data, to.data, 13) == 0);
    from.pos = to.pos = 0;
    assert(stream_copy(&from, &to, 1200) == 1200);
    assert(memcmp(from.data, to.data, 1200) == 0 && buffer_size == 512);
    from.pos = to.pos = 0; from.limit = 3;
    assert(stream_copy(&from, &to, 13) == 0 && from.pos == 3 && to.pos == 0);
    from.pos = to.pos = 0; from.limit = 2048; to.limit = 3;
    assert(stream_copy(&from, &to, 13) == 0 && from.pos == 13 && to.pos == 3);
    from.pos = to.pos = 0; to.limit = 2048; from.size = 520;
    assert(stream_copy(&from, &to, 600) == 512 && from.pos == 520 && to.pos == 512);
    return 0;
}
'''
        result = run_c(harness)
        if not os.environ.get("OPTIMIZATION_BASELINE_REV"):
            self.assertIn("empty copy: allocations=0", result)
            self.assertIn("13-byte copy: allocated_bytes=13", result)
        print(result, end="")

    def test_multiline_rendering(self):
        code = source("applications/services/gui/elements.c")
        start = code.rfind("static size_t", 0, code.index("elements_get_max_chars_to_fit"))
        end = code.index("\nvoid ", code.index("void elements_multiline_text_aligned") + 5)
        harness = PREAMBLE + r'''
typedef enum { AlignLeft, AlignRight, AlignCenter, AlignTop, AlignBottom } Align;
typedef struct { size_t width, height; } Canvas;
static size_t canvas_width(Canvas* c) { return c->width; }
static size_t canvas_height(Canvas* c) { return c->height; }
static size_t canvas_current_font_height(Canvas* c) { (void)c; return 8; }
static size_t canvas_string_width(Canvas* c, const char* s) { (void)c; return strlen(s) * 6; }
static char trace[8192];
static void canvas_draw_str_aligned(Canvas* c, int32_t x, int32_t y, Align h, Align v, const char* s) {
    (void)c; (void)h; (void)v;
    snprintf(trace + strlen(trace), sizeof(trace) - strlen(trace), "%d,%d:%s|", x, y, s);
}
''' + code[start:end] + r'''
static void check(Canvas* c, int x, int y, Align h, Align v, const char* text, const char* expected) {
    trace[0] = 0;
    elements_multiline_text_aligned(c, x, y, h, v, text);
    assert(strcmp(trace, expected) == 0);
    assert(allocations == frees);
}
int main(void) {
    Canvas c = {128, 64};
    check(&c, 0, 8, AlignLeft, AlignTop, "one\ntwo\nthree", "0,8:one|0,16:two|0,24:three|");
    printf("three lines: allocations=%u measured_bytes_copied=%u\n", allocations, copied);
    check(&c, 64, 32, AlignCenter, AlignCenter, "one\ntwo", "64,28:one|64,36:two|");
    check(&c, 128, 32, AlignRight, AlignBottom, "one\ntwo\n", "128,24:one|128,32:two|");
    check(&c, 0, 8, AlignLeft, AlignTop, "", "");
    check(&c, 0, 8, AlignLeft, AlignTop, "\nA\n", "0,8:|0,16:A|");
    check(&c, 0, 8, AlignLeft, AlignTop, "abcdefghijklmnopqrstuvwxyz", "0,8:abcdefghijklmnopqrs-\n|0,16:tuvwxyz|");
    check(&c, 0, 60, AlignLeft, AlignTop, "abcdefghijklmnopqrstuvwxyz", "0,60:abcdefghijklmnopqrst...\n|");
    c.width = 64; c.height = 128;
    check(&c, 32, 8, AlignCenter, AlignTop, "abcdefghijklmno", "32,8:abcdefgh-\n|32,16:ijklmno|");
    return 0;
}
'''
        result = run_c(harness)
        if not os.environ.get("OPTIMIZATION_BASELINE_REV"):
            self.assertIn("allocations=1 measured_bytes_copied=22", result)
        print(result, end="")


if __name__ == "__main__":
    unittest.main()
