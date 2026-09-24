"""Compile production functions on the host; HOT_PATH_ASAN=1 enables ASan."""

import unittest

from test_hot_path_optimization import PREAMBLE, run_c, source


def function(path, signature):
    text = source(path)
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


GUI = r'''
typedef int Canvas;
enum { AlignCenter, AlignBottom, FontSecondary, FontPrimary, ColorBlack, ColorWhite };
static char drawn[8192];
int draw_x, draw_y, draw_count;
static size_t canvas_string_width(Canvas* c, const char* s) { (void)c; return strlen(s); }
static void canvas_draw_str(Canvas* c, int x, int y, const char* s) {
    (void)c; strcpy(drawn, s); draw_x = x; draw_y = y; ++draw_count;
}
static void canvas_draw_str_aligned(Canvas* c, int x, int y, int a, int b, const char* s) {
    (void)a; (void)b; canvas_draw_str(c, x, y, s);
}
static size_t canvas_glyph_width(Canvas* c, char ch) { (void)c; (void)ch; return 1; }
static size_t furi_string_size(const FuriString* s) { return strlen(s->data); }
static char furi_string_get_char(const FuriString* s, size_t i) { return s->data[i]; }
static void furi_string_right(FuriString* s, size_t i) {
    size_t len = strlen(s->data); if(i > len) i = len;
    memmove(s->data, s->data + i, len - i + 1);
}
static void furi_string_cat(FuriString* s, const char* text) { strcat(s->data, text); }
#define furi_string_alloc_set(s) furi_string_alloc_set((const char*)(s))
struct { bool scroll_marquee; } momentum_settings;
'''


class ApplicationsAuditTest(unittest.TestCase):
    def test_button_text_allocation_and_output(self):
        elements = "applications/services/gui/elements.c"
        code = function(elements, "void elements_string_fit_width(")
        code += function(elements, "void elements_scrollable_text_line_centered(")
        code += function(elements, "void elements_scrollable_text_line(")
        code += function("applications/services/gui/modules/button_menu.c", "static void button_menu_draw_text(")
        run_c(PREAMBLE + GUI + r'''
#define ITEM_WIDTH 64
#define ITEM_HEIGHT 14
typedef struct { size_t scroll_counter; } ButtonMenuModel;
''' + code + r'''
int main(void) {
    Canvas c = 0; ButtonMenuModel m = {0}; char text[100];
    for(int selected = 0; selected < 2; ++selected) {
        for(size_t length = 0; length < sizeof(text); ++length) {
            memset(text, 'a', length); text[length] = 0;
            allocations = frees = copied = 0;
            button_menu_draw_text(&c, 0, 17, text, selected, &m);
            if(length < 56 || (!selected && length <= 58)) {
                assert(allocations == 0 && copied == 0 && !strcmp(drawn, text));
                assert(draw_x == 32 && draw_y == 24);
            } else if(selected) {
                assert(strlen(drawn) == 56 && draw_x == 4 && draw_y == 27);
            } else {
                assert(strlen(drawn) == 58 && !strcmp(drawn + 55, "..."));
            }
            assert(allocations == frees);
        }
    }
}
''')

    def test_ptt_menu_failure_unlock_and_item_copy(self):
        path = "applications/system/hid_app/views/hid_ptt_menu.c"
        code = "\n".join(function(path, signature) for signature in [
            "static void PushToTalkMenuItem_init(",
            "static void PushToTalkMenuItem_init_set(",
            "static void PushToTalkMenuItem_set(",
            "PushToTalkMenuList* hid_ptt_menu_get_list_at_index(",
            "void ptt_menu_add_list(",
            "void ptt_menu_add_item_to_list(",
        ])
        run_c(PREAMBLE + GUI + r'''
typedef void (*PushToTalkMenuItemCallback)(void);
typedef struct { FuriString* label; uint32_t index; PushToTalkMenuItemCallback callback; void* callback_context; } PushToTalkMenuItem;
typedef struct { PushToTalkMenuItem item; } ItemArray;
typedef struct { FuriString* label; uint32_t index; ItemArray items; } PushToTalkMenuList;
typedef struct { PushToTalkMenuList* lists; int lists_count; } HidPushToTalkMenuModel;
typedef struct { HidPushToTalkMenuModel* view; } HidPushToTalkMenu;
static int locks;
static bool fail_realloc;
static void* view_get_model(void* view) { assert(locks == 0); ++locks; return view; }
static void view_commit_model(void* view, bool update) { (void)view; (void)update; --locks; }
#define with_view_model(view, type, code, update) { type = view_get_model(view); {code}; view_commit_model(view, update); }
#define furi_assert(x) assert(x)
#define FURI_LOG_E(...) ((void)0)
#define UNUSED(x) ((void)(x))
static void* test_realloc(void* p, size_t n) { return fail_realloc ? NULL : realloc(p, n); }
#define realloc test_realloc
static void furi_string_set(FuriString* d, const FuriString* s) { strcpy(d->data, s->data); }
static void furi_string_set_str(FuriString* d, const char* s) { strcpy(d->data, s); }
static void PushToTalkMenuItemArray_init(ItemArray a) { (void)a; }
static PushToTalkMenuItem added;
static PushToTalkMenuItem* PushToTalkMenuItemArray_push_new(ItemArray a) { (void)a; return &added; }
static void callback(void) {}
''' + code + r'''
int main(void) {
    HidPushToTalkMenuModel m = {0}; HidPushToTalkMenu menu = {&m};
    fail_realloc = true;
    ptt_menu_add_list(&menu, "fail", 1); assert(locks == 0 && m.lists_count == 0 && !m.lists);
    fail_realloc = false; ptt_menu_add_list(&menu, "ok", 2);
    assert(locks == 0 && m.lists_count == 1);
    PushToTalkMenuList* saved = m.lists;
    fail_realloc = true; ptt_menu_add_list(&menu, "fail", 3);
    assert(locks == 0 && m.lists_count == 1 && m.lists == saved);
    ptt_menu_add_item_to_list(&menu, 123, "unknown", 0, callback, &m); assert(locks == 0);
    PushToTalkMenuItem_init(&added);
    assert(!added.callback && !added.callback_context);
    ptt_menu_add_item_to_list(&menu, 2, "item", 10, callback, &m);
    assert(locks == 0 && added.callback == callback && added.callback_context == &m);
    PushToTalkMenuItem copied_item; PushToTalkMenuItem_init_set(&copied_item, &added);
    assert(copied_item.callback == callback && copied_item.callback_context == &m);
    added.callback_context = &menu; PushToTalkMenuItem_set(&copied_item, &added);
    assert(copied_item.callback_context == &menu && copied_item.index == 10);
    furi_string_free(added.label); furi_string_free(copied_item.label);
    furi_string_free(m.lists[0].label); free(m.lists);
}
''')

    def test_sonicare_output(self):
        path = "applications/main/nfc/plugins/supported_cards/sonicare.c"
        text = source(path)
        code = text[text.index("typedef enum {"):text.index("/* Actual implementation")]
        run_c(PREAMBLE + r'''
typedef struct { struct { uint8_t data[4]; } page[40]; } MfUltralightData;
typedef MfUltralightData NfcDevice;
#define NfcProtocolMfUltralight 0
#define furi_assert(x) assert(x)
#define FURI_LOG_D(...) ((void)0)
static const MfUltralightData* nfc_device_get_data(const NfcDevice* d, int p) { (void)p; return d; }
''' + code + r'''
int main(void) {
    MfUltralightData data = {0}; FuriString out;
    strcpy((char*)data.page + 23, "philips.com/nfcbrushheadtap");
    for(unsigned color = 0; color < 2; ++color) {
        data.page[34].data[0] = color ? 0x30 : 0;
        for(unsigned seconds = 0; seconds <= 65535; ++seconds) {
            data.page[36].data[0] = seconds & 255; data.page[36].data[1] = seconds >> 8;
            assert(sonicare_parse(&data, &out));
            char expected[200];
            snprintf(expected, sizeof(expected), "\033#Philips Sonicare head\nColor: %s\nTime brushed: %02.0f:%02.0f:%02ld\n",
                color ? "White" : "Unknown", floor(seconds / 3600), floor((seconds / 60) % 60), (long)(seconds % 60));
            assert(!strcmp(expected, out.data));
        }
    }
    assert(allocations == 0);
}
''')

    def test_neofetch_missing_storage(self):
        text = source("applications/services/cli/commands/neofetch.c")
        code = text[text.index("    // Format hostname delimiter"):text.index("    // Get battery info")]
        run_c(PREAMBLE + r'''
#define RECORD_STORAGE 0
#define FSE_OK 0
typedef int Storage;
static int status;
static uint64_t total, available;
static char hostname[100];
static const char* furi_hal_version_get_name_ptr(void) { return hostname; }
static size_t memmgr_get_total_heap(void) { return 100; }
static size_t memmgr_get_free_heap(void) { return 50; }
static Storage* furi_record_open(int r) { (void)r; static Storage s; return &s; }
static void furi_record_close(int r) { (void)r; }
static int storage_common_fs_info(Storage* s, const char* path, uint64_t* t, uint64_t* f) {
    (void)s; (void)path; if(status == FSE_OK) { *t = total; *f = available; } return status;
}
static void check(uint64_t expected_total, uint64_t expected_percent) {
''' + code + r'''
    assert(ext_total == expected_total && ext_percent == expected_percent && heap_percent == 50);
    assert(strlen(delimiter) == MIN(strlen(hostname) + 4, 63));
}
int main(void) {
    for(size_t n = 0; n < sizeof(hostname); ++n) {
        memset(hostname, 'x', n); hostname[n] = 0;
        status = 1; check(0, 0);
        status = 0; total = 0; available = 0; check(0, 0);
        total = 16 * 1024 * 1024; available = total / 4; check(16, 75);
        available = total + 1; check(0, 0);
    }
}
''')

    def test_menu_visible_ranges(self):
        for menu, array, signature, end_marker, limit in [
            ("button_menu", "ButtonMenuItemArray", "static void button_menu_view_draw_callback(", "static void button_menu_process_up(", 6),
            ("variable_item_list", "VariableItemArray", "static void variable_item_list_draw_callback(", "void variable_item_list_set_selected_item(", 4),
        ]:
            with self.subTest(menu=menu):
                text = source(f"applications/services/gui/modules/{menu}.c")
                code = text[text.index(signature):text.index(end_marker, text.index(signature))]
                if menu == "button_menu":
                    definitions = r'''
#define BUTTONS_PER_SCREEN 6
#define ITEM_WIDTH 64
enum { ButtonMenuItemTypeControl, ButtonMenuItemTypeCommon };
typedef struct { int type; const char* label; } ButtonMenuItem;
typedef struct { ButtonMenuItem* data; size_t size; } Array;
typedef struct { Array items; size_t position; const char* header; size_t scroll_counter; } ButtonMenuModel;
static void button_menu_draw_control_button(Canvas* c, size_t p, const char* s, bool selected, ButtonMenuModel* m) {
    (void)c; (void)p; (void)s; (void)selected; (void)m; ++rows;
}
#define button_menu_draw_common_button button_menu_draw_control_button
static void elements_string_fit_width(Canvas* c, FuriString* s, size_t w) { (void)c; (void)s; (void)w; }
'''
                    model, item = "ButtonMenuModel", "ButtonMenuItem"
                    setup = "m.header = NULL; m.position = window;"
                    expected = "window / 6 * 6"
                else:
                    definitions = r'''
typedef struct { FuriString* label; FuriString* current_value_text; FuriString* locked_message;
    bool locked; uint8_t current_value_index, values_count; } VariableItem;
typedef struct { VariableItem* data; size_t size; } Array;
typedef struct { Array items; size_t position, window_position; FuriString* header;
    size_t scroll_counter; bool locked_message_visible; } VariableItemListModel;
static size_t variable_item_list_items_on_screen(VariableItemListModel* m) { return m->header->data[0] ? 3 : 4; }
static bool furi_string_empty(FuriString* s) { return !s->data[0]; }
'''
                    model, item = "VariableItemListModel", "VariableItem"
                    setup = "m.header = &empty; m.position = window; m.window_position = window;"
                    expected = "window"
                harness = PREAMBLE + GUI.replace("bool scroll_marquee;", "bool scroll_marquee, popup_overlay;") + r'''
static unsigned rows, visits;
static void noop(int dummy, ...) { (void)dummy; }
static int canvas_width(Canvas* c) { (void)c; return 128; }
#define furi_assert(x) assert(x)
#define canvas_clear(...) noop(0, __VA_ARGS__)
#define canvas_set_font(...) noop(0, __VA_ARGS__)
#define canvas_set_color(...) noop(0, __VA_ARGS__)
#define canvas_draw_icon(...) noop(0, __VA_ARGS__)
#define canvas_draw_box(...) noop(0, __VA_ARGS__)
#define canvas_draw_rframe(...) noop(0, __VA_ARGS__)
#define canvas_draw_overlay(...) noop(0, __VA_ARGS__)
#define elements_slightly_rounded_box(...) noop(0, __VA_ARGS__)
#define elements_scrollbar(...) noop(0, __VA_ARGS__)
#define elements_multiline_text_aligned(...) noop(0, __VA_ARGS__)
static int I_InfraredArrowUp_4x8, I_InfraredArrowDown_4x8, I_Lock_7x8, I_WarningDolphin_45x42;
static void elements_scrollable_text_line(Canvas* c, int x, int y, size_t w, FuriString* s, size_t scroll, bool e) {
    noop(0,c,x,y,w,s,scroll,e); ++rows;
}
static void elements_scrollable_text_line_centered(Canvas* c, int x, int y, size_t w, FuriString* s, size_t scroll, bool e, bool centered) {
    noop(0,c,x,y,w,s,scroll,e,centered);
}
''' + definitions + f'''
static size_t {array}_size(Array a) {{ return a.size; }}
static {item}* {array}_get(Array a, size_t i) {{ assert(i < a.size); ++visits; return &a.data[i]; }}
''' + code + f'''
int main(void) {{
    Canvas c = 0; FuriString empty = {{0}}; (void)empty;
    (void)I_InfraredArrowUp_4x8; (void)I_InfraredArrowDown_4x8; (void)I_Lock_7x8; (void)I_WarningDolphin_45x42;
    {item} items[300] = {{0}};
    {model} m = {{0}}; m.items.data = items;
    for(size_t i = 0; i < 300; ++i) {{
''' + ("items[i].label = &empty; items[i].current_value_text = &empty; items[i].values_count = 1;" if menu != "button_menu" else "items[i].type = i % 2; items[i].label = \"\";") + f'''
    }}
    for(size_t count = 0; count <= 300; ++count) {{
        m.items.size = count;
        for(size_t window = 0; window <= count; ++window) {{
            {setup}
            rows = visits = 0;
            {menu}_{'view_' if menu == 'button_menu' else ''}draw_callback(&c, &m);
            size_t first = {expected};
            size_t expected_rows = first < count ? MIN({limit}, count - first) : 0;
            assert(rows == expected_rows && visits == expected_rows);
        }}
    }}
}}
'''
                run_c(harness)

    def test_fxbm_header_validation(self):
        path = "applications/system/js_app/modules/js_gui/icon.c"
        text = source(path)
        wrapper = text[text.index("typedef struct FURI_PACKED"):text.index("LIST_DEF(")]
        code = function(path, "static void js_gui_icon_load_fxbm(")
        run_c(PREAMBLE + r'''
#define FURI_PACKED
typedef struct { uint16_t width, height; uint8_t frame_count, frame_rate; const uint8_t* const* frames; } Icon;
''' + wrapper + r'''
struct mjs { int error, result; };
typedef int Storage;
typedef struct { uint32_t size, width, height; } Header;
typedef struct { Header header; uint64_t size; bool short_read; } File;
static File file_data;
static unsigned allocated, closed, reads;
typedef struct { int fxbm_list; } JsGuiIconInst;
static JsGuiIconInst instance;
static FxbmIconWrapper* loaded;
#define JS_VALUE_PARSE_ARGS_OR_RETURN(m, a, p) (*(p) = "test.fxbm")
#define JS_ERROR_AND_RETURN(m, e, msg) do { (m)->error = 1; return; } while(0)
#define JS_GET_CONTEXT(m) (&instance)
#define FURI_CONST_ASSIGN(dst, src) ((dst) = (src))
#define FURI_CONST_ASSIGN_PTR(dst, src) ((dst) = (void*)(src))
#define RECORD_STORAGE 0
#define FSAM_READ 0
#define FSOM_OPEN_EXISTING 0
static Storage* furi_record_open(int r) { (void)r; static Storage s; return &s; }
static void furi_record_close(int r) { (void)r; }
static File* storage_file_alloc(Storage* s) { (void)s; return &file_data; }
static bool storage_file_open(File* f, const char* p, int a, int b) { (void)f; (void)p; (void)a; (void)b; return true; }
static size_t storage_file_read(File* f, void* data, size_t n) {
    if(reads++ == 0) { assert(n == sizeof(Header)); memcpy(data, &f->header, n); return n; }
    memset(data, 0xa5, n); return f->short_read ? n - 1 : n;
}
static uint64_t storage_file_size(File* f) { return f->size; }
static void storage_file_free(File* f) { (void)f; ++closed; }
static void* checked_malloc(size_t n) { ++allocated; assert(n < 1024); return malloc(n); }
#define malloc checked_malloc
#define FxbmIconWrapperList_push_back(list, value) ((void)(list), loaded = (value))
static int mjs_mk_foreign(struct mjs* m, void* p) { (void)m; (void)p; return 1; }
static void mjs_return(struct mjs* m, int result) { m->result = result; }
''' + code + r'''
static void check(Header h, uint64_t size, bool short_read, bool valid) {
    file_data = (File){h, size, short_read}; reads = allocated = closed = 0; loaded = NULL;
    struct mjs m = {0}; js_gui_icon_load_fxbm(&m);
    assert(closed == 1 && (m.result != 0) == valid && (m.error == 0) == valid);
    if(valid) { assert(allocated == 1 && loaded->icon.width == h.width); free(loaded); }
    else if(!short_read) assert(allocated == 0);
}
int main(void) {
    check((Header){10, 9, 1}, 14, false, true);
    check((Header){10, 9, 1}, 14, true, false);
    check((Header){7, 1, 1}, 14, false, false);
    check((Header){9, 9, 1}, 13, false, false);
    check((Header){10, 9, 1}, 13, false, false);
    check((Header){10, 0, 1}, 14, false, false);
    check((Header){10, 1, 0}, 14, false, false);
    check((Header){10, 65536, 1}, 14, false, false);
    check((Header){10, 1, 65536}, 14, false, false);
    check((Header){UINT32_MAX, 1, 1}, 14, false, false);
}
''')

    def test_console_wrapping_and_utf8_boundaries(self):
        code = function("applications/system/js_app/views/console_view.c", "void console_view_print(")
        run_c(PREAMBLE + r'''
#define LINE_LEN_MAX 25
#define LINE_BREAKS_MAX 3
typedef struct { char lines[512][26]; bool trimmed[512]; size_t count; } JsConsoleView;
static void console_view_push_line(JsConsoleView* v, const char* s, bool trimmed) {
    assert(strlen(s) <= 25 && v->count < 512);
    strcpy(v->lines[v->count], s); v->trimmed[v->count++] = trimmed;
}
''' + code + r'''
int main(void) {
    JsConsoleView v = {0};
    console_view_print(&v, "hello\nworld");
    assert(v.count == 2 && !strcmp(v.lines[0], "hello") && !strcmp(v.lines[1], "world"));
    char text[2048]; memset(text, 'a', sizeof(text) - 1); text[2047] = 0;
    v.count = 0; console_view_print(&v, text);
    assert(v.count == 3 && v.trimmed[2]);
    assert(strlen(v.lines[0]) == 25 && v.lines[1][0] == ' ');
    // Long runs of 2-, 3-, 4-byte UTF-8 and invalid high bytes used to overrun line_buf.
    const char* sequences[] = {"\xc2\xa3", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xff"};
    for(size_t s = 0; s < 4; ++s) {
        for(size_t prefix = 0; prefix <= 25; ++prefix) {
            memset(text, 'a', prefix); size_t n = prefix;
            for(size_t i = 0; i < 100; ++i) {
                memcpy(text + n, sequences[s], strlen(sequences[s])); n += strlen(sequences[s]);
            }
            text[n] = 0; v.count = 0; console_view_print(&v, text);
            assert(v.count == 3 && v.trimmed[2]);
        }
    }
    // Incomplete UTF-8 followed by ASCII at a line boundary.
    memset(text, 'a', 24); strcpy(text + 24, "\xc2Z");
    v.count = 0; console_view_print(&v, text);
    assert(v.count == 2 && v.lines[0][24] == '?' && !strcmp(v.lines[1], " Z"));
    v.count = 0; console_view_print(&v, ""); assert(v.count == 0);
}
''')

    def test_text_input_cursor_buffer(self):
        code = function("applications/services/gui/modules/text_input.c", "static void text_input_view_draw_callback(")
        code = code[:code.index("    canvas_set_font(canvas, FontKeyboard);")] + "}\n"
        run_c(PREAMBLE + GUI + r'''
typedef struct { char* text_buffer; size_t cursor_pos; const char* header; bool clear_default_text; } TextInputModel;
static size_t strlcpy(char* d, const char* s, size_t n) {
    size_t len = strlen(s); if(n) { size_t k = MIN(len, n - 1); memcpy(d, s, k); d[k] = 0; } return len;
}
static size_t strlcat(char* d, const char* s, size_t n) {
    size_t len = strlen(d); return len + strlcpy(d + len, s, n - len);
}
static int canvas_width(Canvas* c) { (void)c; return 128; }
#define canvas_clear(c) ((void)(c))
#define canvas_set_color(c, color) ((void)(c), (void)(color))
#define elements_slightly_rounded_frame(c, x, y, w, h) ((void)(c))
#define elements_slightly_rounded_box(c, x, y, w, h) ((void)(c))
''' + code + r'''
int main(void) {
    Canvas c = 0; char text[301];
    TextInputModel m = {.header = "", .text_buffer = text};
    for(size_t len = 0; len <= 300; ++len) {
        memset(text, 'a', len); text[len] = 0;
        for(size_t cursor = 0; cursor <= len; ++cursor) {
            m.cursor_pos = cursor; m.clear_default_text = false;
            text_input_view_draw_callback(&c, &m);
            assert(m.cursor_pos == cursor && strlen(text) == len);
            if(len < 100) { assert(strlen(drawn) == len + 1 && drawn[cursor] == '|'); }
        }
        m.clear_default_text = true; text_input_view_draw_callback(&c, &m);
    }
    m.text_buffer = NULL; m.clear_default_text = false; m.cursor_pos = 10;
    text_input_view_draw_callback(&c, &m); assert(!strcmp(drawn, "|"));
}
''')

    def test_scrolling_text_short_path(self):
        code = function("applications/services/gui/elements.c", "void elements_scrollable_text_line_centered(")
        run_c(PREAMBLE + GUI + code + r'''
int main(void) {
    Canvas c = 0; FuriString s = {"short"};
    for(int centered = 0; centered < 2; ++centered) {
        for(int marquee = 0; marquee < 2; ++marquee) {
            momentum_settings.scroll_marquee = marquee;
            for(size_t scroll = 0; scroll < 20; ++scroll) {
                elements_scrollable_text_line_centered(&c, 12, 34, 5, &s, scroll, true, centered);
                assert(!strcmp(drawn, "short") && draw_x == 12 && draw_y == 34);
            }
        }
    }
    assert(allocations == 0 && copied == 0);
    strcpy(s.data, "abcdefghijklmno");
    for(int marquee = 0; marquee < 2; ++marquee) {
        momentum_settings.scroll_marquee = marquee;
        for(size_t scroll = 0; scroll < 100; ++scroll) {
            elements_scrollable_text_line_centered(&c, 12, 34, 8, &s, scroll, true, true);
            assert(strlen(drawn) <= 8 && !strcmp(drawn + strlen(drawn) - 3, "..."));
            assert(!strcmp(s.data, "abcdefghijklmno"));
        }
    }
    assert(allocations == frees);
}
''')

    def test_nfc_decimal_conversion(self):
        code = function("applications/main/nfc/plugins/supported_cards/social_moscow.c", "static uint64_t hex_num(")
        run_c(PREAMBLE + code + r'''
int main(void) {
    uint64_t state = 1;
    for(size_t sample = 0; sample < 100000; ++sample) {
        state = state * 6364136223846793005ULL + 1;
        uint64_t expected = 0, scale = 1, input = state;
        for(size_t digit = 0; digit < 8; ++digit) {
            expected += (input & 15) * scale; input >>= 4; scale *= 10;
        }
        assert(hex_num(state) == expected);
    }
    assert(hex_num(0) == 0 && hex_num(0x12345678) == 12345678);
}
''')


if __name__ == "__main__":
    unittest.main()
