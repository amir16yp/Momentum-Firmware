#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _ATTRIBUTE(x) __attribute__(x)
#include <furi/core/string.h>
#include <m-string.h>
static size_t allocations;
void* tracked_alloc(size_t size) {
    void* result = malloc(size);
    assert(result);
    allocations++;
    return result;
}
void tracked_free(void* ptr) {
    if(ptr) {
        assert(allocations);
        allocations--;
        free(ptr);
    }
}
#define malloc tracked_alloc
#define free tracked_free
typedef struct {
    void* data;
    void (*print_strn)(void*, const char*, size_t);
} mp_print_t;
static void* mp_flipper_print_data_alloc(void) { return furi_string_alloc(); }
static void mp_flipper_print_data_free(void* data) { furi_string_free(data); }
static const char* mp_flipper_print_get_data(void* data) { return furi_string_get_cstr(data); }
static void mp_flipper_print_strn(void* data, const char* str, size_t len) {
    for(size_t i = 0; i < len; ++i) furi_string_push_back(data, str[i]);
}
static size_t completion_mode;
static size_t mp_flipper_repl_autocomplete(
    const char* str, size_t len, const mp_print_t* print, const char** completion) {
    (void)str;
    (void)len;
    if(completion_mode == (size_t)-1) print->print_strn(print->data, "matches", 7);
    // Borrowed and deliberately not terminated at the returned length.
    *completion = "nt_more";
    return completion_mode;
}
/* REPL */
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_D(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
static unsigned init_calls, exec_calls, deinit_calls;
static size_t memmgr_get_free_heap(void) { return 10000; }
static void mp_flipper_set_root_module_path(const char* path) { assert(!strcmp(path, "/ext")); }
static void mp_flipper_init(void* heap, size_t size, size_t stack_size, void* stack) {
    assert(heap && size == 1000 && stack_size == 2048 && stack);
    init_calls++;
}
static void mp_flipper_exec_py_file(const char* path) {
    assert(!strcmp(path, "/ext/test.py"));
    exec_calls++;
}
static void mp_flipper_deinit(void) { deinit_calls++; }
/* FILE */
int main(void) {
    for(unsigned cycle = 0; cycle < 20; ++cycle) {
        mp_flipper_repl_context_t* ctx = mp_flipper_repl_context_alloc();
        for(unsigned i = 0; i < 40; ++i) {
            furi_string_printf(ctx->line, "command %u with a long argument", i);
            update_history(ctx);
        }
        assert(ctx->history->size == HISTORY_SIZE);
        for(unsigned i = 1; i < HISTORY_SIZE; ++i) {
            char expected[80];
            snprintf(expected, sizeof(expected), "command %u with a long argument", 40 - i);
            assert(!strcmp(furi_string_get_cstr(ctx->history->stack[i]), expected));
        }
        FuriString* latest = ctx->history->stack[1];
        update_history(ctx);
        assert(ctx->history->stack[1] == latest);
        furi_string_set_str(ctx->line, "draft");
        handle_arrow_keys('A', ctx);
        handle_arrow_keys('B', ctx);
        assert(!strcmp(furi_string_get_cstr(ctx->line), "draft"));
        ctx->cursor = 3;
        handle_backspace(ctx);
        assert(!strcmp(furi_string_get_cstr(ctx->line), "drft") && ctx->cursor == 2);
        ctx->cursor = 0;
        handle_backspace(ctx);
        assert(!strcmp(furi_string_get_cstr(ctx->line), "drft"));
        furi_string_set_str(ctx->line, "pri(123)");
        ctx->cursor = 3;
        completion_mode = 2;
        handle_autocomplete(ctx);
        assert(!strcmp(furi_string_get_cstr(ctx->line), "print(123)") && ctx->cursor == 5);
        completion_mode = 0;
        handle_autocomplete(ctx);
        completion_mode = (size_t)-1;
        handle_autocomplete(ctx);
        assert(!strcmp(furi_string_get_cstr(ctx->line), "print(123)"));
        ctx->is_ps2 = true;
        ctx->cursor = 0;
        handle_autocomplete(ctx);
        assert(!strcmp(furi_string_get_cstr(ctx->line), "    print(123)"));
        mp_flipper_repl_context_free(ctx);
        assert(allocations == 0);
    }
    const char* paths[] = {"test.py", "/ext/test.txt", "/ext/test.py"};
    for(size_t i = 0; i < 3; ++i) {
        FuriString* path = furi_string_alloc_set_str(paths[i]);
        upython_file_execute(path);
        assert(!strcmp(furi_string_get_cstr(path), paths[i]));
        furi_string_free(path);
        assert(allocations == 0);
    }
    assert(init_calls == 1 && exec_calls == 1 && deinit_calls == 1);
    return 0;
}
