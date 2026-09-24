"""Compile storage functions with fault-injecting host stubs (no device required)."""

from pathlib import Path
import unittest

from test_hot_path_optimization import run_c

ROOT = Path(__file__).resolve().parents[2]
STORAGE = ROOT / "applications/services/storage"
PREAMBLE = r"""
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define furi_check assert
#define FURI_LOG_T(...) ((void)0)
#define UNUSED(x) ((void)(x))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
enum { FSE_OK, FSE_ALREADY_OPEN, FSE_NOT_EXIST, FSE_INTERNAL };
typedef int FS_Error;
"""


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == "{") - (source[end] == "}")
    return source[start:end + 1]


class StorageOptimizationTest(unittest.TestCase):
    def test_copy_failures(self):
        source = (STORAGE / "storage_external_api.c").read_text()
        code = PREAMBLE + r"""
typedef int Storage;
typedef struct { int error; } Stream;
enum { FSAM_READ, FSAM_WRITE, FSOM_OPEN_EXISTING, FSOM_CREATE_NEW };
static Stream streams[2];
static int allocated, freed, scenario;
static Stream* file_stream_alloc(Storage* storage) {
    UNUSED(storage); assert(allocated < 2); return &streams[allocated++];
}
static bool file_stream_open(Stream* stream, const char* path, int access, int mode) {
    UNUSED(path); UNUSED(access); UNUSED(mode);
    if((scenario == 1 && stream == &streams[0]) ||
       (scenario == 2 && stream == &streams[1])) {
        stream->error = FSE_NOT_EXIST; return false;
    }
    return true;
}
static size_t stream_size(Stream* stream) { UNUSED(stream); return scenario == 5 ? 0 : 1024; }
static size_t stream_copy(Stream* from, Stream* to, size_t size) {
    if(scenario == 3) { from->error = FSE_INTERNAL; return 0; }
    if(scenario == 4) return size / 2; // Disk full, but FatFs error is still OK.
    if(scenario == 6) { to->error = FSE_INTERNAL; return 0; }
    return size;
}
static int file_stream_get_error(Stream* stream) { return stream->error; }
static void stream_free(Stream* stream) { UNUSED(stream); ++freed; }
"""
        code += function(source, "static FS_Error storage_copy_file(")
        code += r"""
int main(void) {
    const int expected[] = {FSE_OK, FSE_NOT_EXIST, FSE_NOT_EXIST, FSE_INTERNAL,
                            FSE_INTERNAL, FSE_OK, FSE_INTERNAL};
    for(scenario = 0; scenario < 7; ++scenario) {
        allocated = freed = 0; memset(streams, 0, sizeof(streams));
        assert(storage_copy_file(NULL, "source", "destination") == expected[scenario]);
        assert(allocated == 2 && freed == 2);
    }
}
"""
        run_c(code)

    def test_open_notification_races(self):
        source = (STORAGE / "storage_external_api.c").read_text()
        code = PREAMBLE + r"""
typedef struct { int error_id; void* storage; } File;
typedef int FS_AccessMode;
typedef int FS_OpenMode;
typedef int FuriEventFlag;
typedef int FuriPubSubSubscription;
enum { StorageEventFlagFileClose = 1, FuriFlagWaitAny, FuriWaitForever };
static int attempts, allocations, subscriptions, waits, live, scenario;
static FuriEventFlag event;
static void storage_file_close_callback(const void* message, void* context) {
    UNUSED(message); UNUSED(context);
}
static void* storage_get_pubsub(void* storage) { return storage; }
static FuriEventFlag* furi_event_flag_alloc(void) { ++allocations; ++live; return &event; }
static void furi_event_flag_free(FuriEventFlag* e) { assert(e == &event); --live; }
static FuriPubSubSubscription* furi_pubsub_subscribe(
    void* pubsub, void (*cb)(const void*, void*), void* ctx) {
    UNUSED(pubsub); UNUSED(cb); assert(ctx == &event); ++subscriptions; ++live; return &event;
}
static void furi_pubsub_unsubscribe(void* pubsub, FuriPubSubSubscription* sub) {
    UNUSED(pubsub); assert(sub == &event); --live;
}
static void furi_event_flag_wait(FuriEventFlag* e, int flag, int mode, int timeout) {
    UNUSED(flag); UNUSED(mode); UNUSED(timeout);
    assert(e == &event && subscriptions == 1 && attempts >= 2);
    ++waits;
    assert(waits < 4);
}
static bool storage_dir_open_internal(File* file, const char* path) {
    UNUSED(path); ++attempts;
    // Success, permanent failure, close before subscribe, repeated wakeups, retry error.
    if(scenario == 1 || (scenario == 4 && attempts == 2)) file->error_id = FSE_INTERNAL;
    else if((scenario == 2 && attempts == 1) ||
            (scenario == 3 && attempts < 4) || (scenario == 4 && attempts == 1))
        file->error_id = FSE_ALREADY_OPEN;
    else file->error_id = FSE_OK;
    return file->error_id == FSE_OK;
}
static bool storage_file_open_internal(File* file, const char* path, int access, int mode) {
    UNUSED(access); UNUSED(mode); return storage_dir_open_internal(file, path);
}
"""
        code += function(source, "bool storage_file_open(")
        code += function(source, "bool storage_dir_open(")
        code += r"""
int main(void) {
    for(int directory = 0; directory < 2; ++directory) {
        for(scenario = 0; scenario < 5; ++scenario) {
            attempts = allocations = subscriptions = waits = live = 0;
            File file = {0};
            bool ok = directory ? storage_dir_open(&file, "/ext/test") :
                storage_file_open(&file, "/ext/test", 0, 0);
            assert(ok == (scenario != 1 && scenario != 4));
            assert(live == 0);
            assert(allocations == (scenario >= 2));
            assert(subscriptions == (scenario >= 2));
            assert(waits == (scenario == 3 ? 2 : 0));
        }
    }
}
"""
        run_c(code)

    def test_directory_errors_and_truncation(self):
        source = (STORAGE / "storages/storage_ext.c").read_text()
        code = PREAMBLE + r"""
enum { FR_OK, FR_DISK_ERR, AM_DIR = 16, FSF_DIRECTORY = 1 };
typedef int StorageData;
typedef int SDDir;
typedef int SDError;
typedef struct { int error_id, internal_error_id; } File;
typedef struct { uint64_t size; unsigned flags; } FileInfo;
typedef struct { uint32_t fsize; unsigned fattrib; char fname[32]; } SDFileInfo;
static int result, at_end;
static FS_Error storage_ext_parse_error(int error) { return error ? FSE_INTERNAL : FSE_OK; }
static void* storage_get_storage_file_data(File* file, StorageData* storage) {
    UNUSED(file); return storage;
}
static int f_readdir(SDDir* dir, SDFileInfo* info) {
    UNUSED(dir);
    if(result) return result; // Output is undefined on error, as in FatFs.
    info->fname[0] = 0;
    if(!at_end) { strcpy(info->fname, "example.txt"); info->fsize = 42; info->fattrib = AM_DIR; }
    return FR_OK;
}
static size_t strlcpy(char* dst, const char* src, size_t capacity) {
    size_t length = strlen(src);
    if(capacity) { size_t n = MIN(length, capacity - 1); memcpy(dst, src, n); dst[n] = 0; }
    return length;
}
static char* storage_ext_drive_path(StorageData* storage, const char* path) {
    UNUSED(storage); char* p = malloc(strlen(path) + 1); strcpy(p, path); return p;
}
static int f_stat(const char* path, SDFileInfo* info) {
    UNUSED(path); return f_readdir(NULL, info);
}
"""
        code += function(source, "static bool storage_ext_dir_read(")
        code += function(source, "static FS_Error storage_ext_common_stat(")
        code += r"""
int main(void) {
    StorageData storage = 0;
    File file = {0};
    FileInfo info = {99, 77};
    char name[8] = "canary";
    result = FR_DISK_ERR;
    assert(!storage_ext_dir_read(&storage, &file, &info, name, sizeof(name)));
    assert(file.error_id == FSE_INTERNAL && file.internal_error_id == FR_DISK_ERR);
    assert(info.size == 99 && info.flags == 77 && !strcmp(name, "canary"));
    assert(storage_ext_common_stat(&storage, "/bad", &info) == FSE_INTERNAL);
    assert(info.size == 99 && info.flags == 77);
    result = FR_OK; at_end = 1;
    assert(!storage_ext_dir_read(&storage, &file, &info, name, sizeof(name)));
    assert(file.error_id == FSE_NOT_EXIST && info.size == 99);
    at_end = 0;
    assert(storage_ext_dir_read(&storage, &file, &info, name, sizeof(name)));
    assert(!strcmp(name, "example") && info.size == 42 && info.flags == FSF_DIRECTORY);
    assert(storage_ext_dir_read(&storage, &file, NULL, name, 0));
    assert(!strcmp(name, "example"));
    assert(storage_ext_dir_read(&storage, &file, NULL, NULL, 0));
    assert(storage_ext_common_stat(&storage, "/ok", &info) == FSE_OK);
    assert(info.size == 42);
}
"""
        run_c(code)

    def test_cli_transfer_boundaries(self):
        source = (STORAGE / "storage_cli.c").read_text()
        code = PREAMBLE + r"""
typedef int PipeSide;
typedef int Storage;
typedef const char FuriString;
typedef struct { int error; } File;
enum { CliKeyETX = 3, FSAM_READ, FSAM_WRITE, FSOM_OPEN_EXISTING, FSOM_OPEN_APPEND,
       StrintParseNoError };
#define RECORD_STORAGE 0
static File file;
static unsigned char input[2048], output[2048];
static size_t input_size, input_pos, output_size, max_allocation, read_calls, errors;
static size_t reported_size;
static bool fail_read;
static void* checked_malloc(size_t size) {
    if(size > max_allocation) max_allocation = size;
    assert(size <= 512); return malloc(size);
}
#define malloc checked_malloc
#define printf(...) ((void)0)
#define fflush(...) ((void)0)
static int mock_getchar(void) { return input_pos < input_size ? input[input_pos++] : EOF; }
static void mock_putchar(int c) { UNUSED(c); }
#define getchar mock_getchar
#define putchar mock_putchar
static void* furi_record_open(int record) { UNUSED(record); return &file; }
static void furi_record_close(int record) { UNUSED(record); }
static File* storage_file_alloc(Storage* storage) { UNUSED(storage); return &file; }
static void storage_file_free(File* f) { assert(f == &file); }
static bool storage_file_open(File* f, const char* path, int access, int mode) {
    UNUSED(f); UNUSED(path); UNUSED(access); UNUSED(mode); return true;
}
static void storage_file_close(File* f) { UNUSED(f); }
static const char* furi_string_get_cstr(FuriString* s) { return s; }
static size_t storage_file_write(File* f, const void* buffer, size_t size) {
    UNUSED(f); assert(output_size + size <= sizeof(output));
    memcpy(output + output_size, buffer, size); output_size += size; return size;
}
static int storage_file_get_error(File* f) { return f->error; }
static void storage_cli_print_error(int error) { UNUSED(error); ++errors; }
static void storage_cli_print_usage(void) { assert(false); }
static int strint_to_uint32(const char* s, void* end, uint32_t* value, int base) {
    UNUSED(end); *value = (uint32_t)strtoul(s, NULL, base); return StrintParseNoError;
}
static uint64_t storage_file_size(File* f) { UNUSED(f); return reported_size; }
static size_t storage_file_read(File* f, void* buffer, size_t size) {
    ++read_calls; assert(read_calls < 10);
    if(fail_read) { f->error = FSE_INTERNAL; return 0; }
    size = MIN(size, 1500 - output_size);
    memset(buffer, 'x', size); return size;
}
static size_t furi_thread_stdout_write(const char* buffer, size_t size) {
    return storage_file_write(&file, buffer, size);
}
"""
        code += function(source, "static void storage_cli_write(")
        code += function(source, "static void storage_cli_read_chunks(")
        code += r"""
int main(void) {
    const size_t lengths[] = {0, 1, 511, 512, 513, 1024};
    for(size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); ++i) {
        for(int eof = 0; eof < 2; ++eof) {
            size_t n = lengths[i];
            memset(input, 'a', n); input[n] = CliKeyETX;
            input_size = n + !eof; input_pos = output_size = 0;
            storage_cli_write(NULL, "/ext/test", "");
            assert(output_size == n && !memcmp(input, output, n));
        }
    }
    // Huge requested chunks still use a bounded buffer and one acknowledgement.
    input[0] = '\n'; input_size = 1; input_pos = output_size = read_calls = 0;
    reported_size = 1500;
    storage_cli_read_chunks(NULL, "/ext/test", "4294967295");
    assert(output_size == 1500 && read_calls == 3 && input_pos == 1);
    assert(max_allocation == 512);
    // Unexpected EOF and I/O errors terminate instead of repeatedly prompting.
    input_pos = output_size = read_calls = 0; reported_size = 2000;
    storage_cli_read_chunks(NULL, "/ext/test", "2048");
    assert(output_size == 1500 && read_calls == 3);
    input_pos = output_size = read_calls = 0; fail_read = true;
    storage_cli_read_chunks(NULL, "/ext/test", "2048");
    assert(read_calls == 1 && errors == 1);
}
"""
        run_c(code)

    def test_short_paths(self):
        source = (STORAGE / "storage_processing.c").read_text()
        code = PREAMBLE + r"""
#define STORAGE_PATH_PREFIX_LEN 4u
#define STORAGE_EXT_PATH_PREFIX "/ext"
#define STORAGE_INT_PATH_PREFIX "/int"
#define STORAGE_MNT_PATH_PREFIX "/mnt"
#define STORAGE_ANY_PATH_PREFIX "/any"
typedef enum { ST_EXT, ST_INT, ST_MNT, ST_ANY, ST_ERROR } StorageType;
typedef struct { const char* data; size_t size; } FuriString;
static const char* furi_string_get_cstr(FuriString* s) { return s->data; }
static size_t furi_string_size(FuriString* s) { return s->size; }
"""
        code += function(source, "static StorageType storage_get_type_by_path(")
        code += r"""
int main(void) {
    const char* paths[] = {"", "/", "/e", "/ex", "/ext", "/int", "/mnt", "/any",
                           "/ext/test", "/extbad", "/bogus"};
    const int expected[] = {ST_ERROR, ST_ERROR, ST_ERROR, ST_ERROR, ST_EXT, ST_INT,
                            ST_MNT, ST_ANY, ST_EXT, ST_ERROR, ST_ERROR};
    for(size_t i = 0; i < sizeof(paths) / sizeof(*paths); ++i) {
        size_t n = strlen(paths[i]);
        char* exact = malloc(n + 1); memcpy(exact, paths[i], n + 1);
        FuriString path = {exact, n};
        assert((int)storage_get_type_by_path(&path) == expected[i]);
        free(exact);
    }
}
"""
        run_c(code)


if __name__ == "__main__":
    unittest.main()
