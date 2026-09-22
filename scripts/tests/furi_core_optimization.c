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
#include <furi/core/log.h>

#define furi_check(x)   assert(x)
#define FuriWaitForever UINT32_MAX
#define FuriStatusOk    0
#define FuriFlagWaitAny 0
#define FuriFlagNoClear 2
#define COUNT_OF(x)     (sizeof(x) / sizeof((x)[0]))
#define FURI_IS_ISR()   false

static unsigned mutex_acquisitions;

typedef struct {
    unsigned depth;
    bool recursive;
} FuriMutex;
enum {
    FuriMutexTypeNormal,
    FuriMutexTypeRecursive
};
static FuriMutex* furi_mutex_alloc(int type) {
    FuriMutex* mutex = calloc(1, sizeof(*mutex));
    assert(mutex);
    mutex->recursive = type == FuriMutexTypeRecursive;
    return mutex;
}
static int furi_mutex_acquire(FuriMutex* mutex, uint32_t timeout) {
    (void)timeout;
    ++mutex_acquisitions;
    assert(!mutex->depth || mutex->recursive);
    ++mutex->depth;
    return 0;
}
static int furi_mutex_release(FuriMutex* mutex) {
    assert(mutex->depth);
    --mutex->depth;
    return 0;
}
static void* furi_mutex_get_owner(FuriMutex* mutex) {
    return mutex->depth ? mutex : NULL;
}
static bool furi_kernel_is_running(void) {
    return true;
}
static unsigned long furi_get_tick(void) {
    return 42;
}

typedef struct {
    uint32_t bits;
} FuriEventFlag;
static unsigned event_allocations, event_frees, event_waits, event_sets;
static void (*wait_hook)(void);
static FuriEventFlag* furi_event_flag_alloc(void) {
    ++event_allocations;
    return calloc(1, sizeof(FuriEventFlag));
}
static void furi_event_flag_free(FuriEventFlag* event) {
    assert(event);
    ++event_frees;
    free(event);
}
static uint32_t furi_event_flag_set(FuriEventFlag* event, uint32_t bits) {
    ++event_sets;
    return event->bits |= bits;
}
static uint32_t
    furi_event_flag_wait(FuriEventFlag* event, uint32_t bits, uint32_t options, uint32_t timeout) {
    assert(options == FuriFlagNoClear && timeout == FuriWaitForever);
    ++event_waits;
    if(!(event->bits & bits)) {
        assert(wait_hook);
        wait_hook();
    }
    assert((event->bits & bits) == bits);
    return event->bits;
}
static unsigned object_allocations;
static void* object_alloc(size_t size) {
    ++object_allocations;
    void* ptr = calloc(1, size);
    assert(ptr);
    return ptr;
}

/* PRODUCTION_CODE */

static int payload;
static void create_delayed(void) {
    // Force dictionary growth while two opens are suspended.
    for(unsigned i = 0; i < 128; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "other-%u", i);
        furi_record_create(name, &payload);
    }
    assert(!furi_record_destroy("delayed"));
    furi_record_create("delayed", &payload);
}
static void second_waiter(void) {
    wait_hook = create_delayed;
    assert(furi_record_open("delayed") == &payload);
    furi_record_close("delayed");
}
static void test_records(void) {
    furi_record_init();
    for(unsigned i = 0; i < 100; ++i) {
        furi_record_create("ready", &payload);
        assert(furi_record_open("ready") == &payload);
        assert(furi_record_open("ready") == &payload);
        assert(!furi_record_destroy("ready"));
        furi_record_close("ready");
        furi_record_close("ready");
        assert(furi_record_destroy("ready"));
    }
    assert(event_allocations == 0 && event_waits == 0 && event_sets == 0);
    wait_hook = second_waiter;
    assert(furi_record_open("delayed") == &payload);
    assert(event_allocations == 1 && event_waits == 2 && event_sets == 1);
    assert(furi_record_open("delayed") == &payload);
    assert(event_waits == 2);
    furi_record_close("delayed");
    furi_record_close("delayed");
    assert(furi_record_destroy("delayed"));
    assert(event_frees == 1);
    for(unsigned i = 0; i < 128; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "other-%u", i);
        assert(furi_record_destroy(name));
    }
    FuriRecordDataDict_clear(furi_record->records);
    free(furi_record->mutex);
    free(furi_record);
}
static void test_strings(void) {
    FuriString* value = furi_string_alloc_set_str("abc");
    unsigned before = object_allocations;
    assert(furi_string_cat_printf(value, "%s:%d", furi_string_get_cstr(value), 7) == 5);
    assert(strcmp(furi_string_get_cstr(value), "abcabc:7") == 0);
    assert(furi_string_cat_printf(value, "%s", "") == 0);
    furi_string_set_str(value, "%s");
    assert(furi_string_cat_printf(value, furi_string_get_cstr(value), "alias") == 5);
    assert(strcmp(furi_string_get_cstr(value), "%salias") == 0);
    furi_string_set_str(value, "prefix");
    assert(furi_string_cat_printf(value, "%01024d", 1) == 1024);
    assert(furi_string_size(value) == 1030);
    assert(furi_string_get_char(value, 1029) == '1');
    assert(object_allocations == before);
    furi_string_free(value);

    value = furi_string_alloc_set_str("Mixed.Py");
    assert(furi_string_end_withi_str(value, ".py"));
    assert(furi_string_end_withi_str(value, ""));
    assert(!furi_string_end_withi_str(value, "longer-than-the-value"));
    assert(!furi_string_end_withi_str(value, ".txt"));
    furi_string_free(value);
}
static char output[4096];
static size_t output_size;
static bool nested_log;
static void capture(const uint8_t* data, size_t size, void* context) {
    assert(context == &payload);
    assert(output_size + size < sizeof(output));
    memcpy(output + output_size, data, size);
    output_size += size;
    output[output_size] = 0;
    if(nested_log) {
        nested_log = false;
        furi_log_print_raw_format(FuriLogLevelInfo, "%s", "nested");
    }
}
static void test_logging(void) {
    furi_log_init();
    FuriLogHandler handler = {.callback = capture, .context = &payload};
    assert(furi_log_add_handler(handler));
    assert(!furi_log_add_handler(handler));
    unsigned before = mutex_acquisitions;
    furi_log_print_format(FuriLogLevelInfo, "test", "%s:%d", "hello", 7);
    assert(mutex_acquisitions == before + 1);
    assert(strcmp(output, "42 \033[0;32m[I][test] \033[0mhello:7\r\n") == 0);
    output_size = 0;
    furi_log_print_format(FuriLogLevelInfo, "long-tag-to-force-heap-storage", "%s", "");
    assert(strcmp(output, "42 \033[0;32m[I][long-tag-to-force-heap-storage] \033[0m\r\n") == 0);
    output_size = 0;
    before = mutex_acquisitions;
    furi_log_print_raw_format(FuriLogLevelInfo, "%01024d", 1);
    assert(mutex_acquisitions == before + 1);
    assert(output_size == 1024 && output[1023] == '1');
    output_size = 0;
    before = mutex_acquisitions;
    furi_log_print_format(FuriLogLevelTrace, "test", "filtered");
    assert(mutex_acquisitions == before);
    assert(output_size == 0);
    nested_log = true;
    furi_log_print_raw_format(FuriLogLevelInfo, "%s", "outer");
    assert(strcmp(output, "outernested") == 0);
    output_size = 0;
    furi_log_tx((const uint8_t*)"direct", 6);
    assert(strcmp(output, "direct") == 0);
    assert(furi_log_remove_handler(handler));
    assert(!furi_log_remove_handler(handler));
    FuriLogHandlersList_clear(furi_log.tx_handlers);
    free(furi_log.mutex);
}
int main(void) {
    test_records();
    test_strings();
    test_logging();
    puts("Furi core allocation, readiness, formatting and logging regressions passed");
    return 0;
}
