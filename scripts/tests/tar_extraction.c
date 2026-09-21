#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microtar.h>
#include <uzlib.h>

#define FILE_BLOCK_SIZE       (10 * 1024)
#define FILE_OPEN_NTRIES      10
#define FILE_OPEN_RETRY_DELAY 25
#define MIN(a, b)             ((a) < (b) ? (a) : (b))
#define UNUSED(x)             (void)(x)
#define furi_check            assert
#define FURI_LOG_W(...)       ((void)0)
#define FURI_LOG_I(...)       ((void)0)
#define FURI_LOG_D(...)       ((void)0)
#define FSAM_WRITE            0
#define FSOM_CREATE_ALWAYS    0
enum {
    CompressTypeHeatshrink,
    CompressTypeGzip
};
typedef struct {
    char bytes[7];
} HeatshrinkStreamHeader;
typedef struct {
    int unused;
} Storage;
typedef struct {
    struct uzlib_uncomp uz;
    uint8_t dict[32768];
    const uint8_t* input;
    size_t input_size, position, fail_at;
} CompressStreamDecoder;
typedef struct {
    bool open;
    size_t written;
} File;
typedef struct {
    char text[512];
} FuriString;
typedef void (*TarArchiveNameConverter)(FuriString*);
typedef struct {
    Storage* storage;
    File* stream;
    mtar_t tar;
    bool (*unpack_cb)(const char*, bool, void*);
    void* unpack_cb_context;
    void (*read_cb)(size_t, size_t, void*);
    void* read_cb_context;
} TarArchive;
typedef struct {
    int type;
    File* stream;
    CompressStreamDecoder* decoder;
    mtar_t* tar;
} CompressedStream;
static size_t live_allocs, buffer_allocs, size_calls, outputs, progress_calls;
static int fail_open, short_write, fail_mkdir;
static bool converting;
static void* tracked_alloc(size_t size) {
    void* result = calloc(1, size);
    assert(result);
    ++live_allocs;
    if(size == FILE_BLOCK_SIZE) ++buffer_allocs;
    return result;
}
static void tracked_free(void* pointer) {
    if(pointer) {
        --live_allocs;
        free(pointer);
    }
}
#define malloc tracked_alloc
#define free   tracked_free
static FuriString* furi_string_alloc(void) {
    return malloc(sizeof(FuriString));
}
static void furi_string_free(FuriString* s) {
    free(s);
}
static void furi_string_set(FuriString* s, const char* text) {
    strcpy(s->text, text);
}
static const char* furi_string_get_cstr(FuriString* s) {
    return s->text;
}
static void path_concat(const char* base, const char* name, FuriString* path) {
    snprintf(path->text, sizeof(path->text), "%s/%s", base, name);
}
static File* storage_file_alloc(Storage* storage) {
    UNUSED(storage);
    return malloc(sizeof(File));
}
static void storage_file_free(File* file) {
    free(file);
}
static bool storage_file_open(File* file, const char* path, int access, int mode) {
    UNUSED(access);
    UNUSED(mode);
    assert(!strncmp(path, "/ext/", 5));
    if(converting) assert(strstr(path, "converted"));
    if(fail_open) return false;
    file->open = true;
    ++outputs;
    return true;
}
static void storage_file_close(File* file) {
    file->open = false;
}
static bool storage_file_is_open(File* file) {
    return file->open;
}
static size_t storage_file_write(File* file, const uint8_t* data, size_t size) {
    assert(size <= FILE_BLOCK_SIZE);
    if(short_write) return size - 1;
    for(size_t i = 0; i < size; ++i)
        assert(data[i] == (file->written + i) % 251);
    file->written += size;
    return size;
}
static size_t storage_file_size(File* file) {
    UNUSED(file);
    ++size_calls;
    return 12345;
}
static size_t storage_file_tell(File* file) {
    UNUSED(file);
    return 1;
}
static bool storage_file_seek(File* file, unsigned pos, bool absolute) {
    UNUSED(file);
    UNUSED(pos);
    UNUSED(absolute);
    return true;
}
static bool storage_simply_mkdir(Storage* storage, const char* path) {
    UNUSED(storage);
    UNUSED(path);
    return !fail_mkdir;
}
static void furi_delay_ms(unsigned delay) {
    UNUSED(delay);
}
static size_t compress_stream_decoder_tell(CompressStreamDecoder* d) {
    return d->position;
}
static bool compress_stream_decoder_rewind(CompressStreamDecoder* d) {
    memset(&d->uz, 0, sizeof(d->uz));
    uzlib_uncompress_init(&d->uz, d->dict, sizeof(d->dict));
    d->uz.source = d->input;
    d->uz.source_limit = d->input + d->input_size;
    d->position = 0;
    return uzlib_gzip_parse_header(&d->uz) == TINF_OK;
}
static bool compress_stream_decoder_read(CompressStreamDecoder* d, uint8_t* out, size_t count) {
    if(d->fail_at && d->position + count > d->fail_at) return false;
    d->uz.dest = out;
    d->uz.dest_limit = out + count;
    if(uzlib_uncompress_chksum(&d->uz) < 0 || d->uz.dest != out + count) return false;
    d->position += count;
    return true;
}
/* SEEK */
static int read_compressed(void* context, void* data, unsigned size) {
    CompressedStream* stream = context;
    return compress_stream_decoder_read(stream->decoder, data, size) ? (int)size : MTAR_EREADFAIL;
}
static const struct mtar_ops ops = {.read = read_compressed, .seek = mtar_compressed_file_seek};
/* EXTRACTION */
static bool filter(const char* name, bool directory, void* context) {
    UNUSED(directory);
    UNUSED(context);
    return strcmp(name, "skip") != 0;
}
static void convert(FuriString* name) {
    strcat(name->text, ".converted");
}
static void progress(size_t current, size_t total, void* context) {
    UNUSED(context);
    assert(current <= total);
    assert(total == 12345);
    ++progress_calls;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    FILE* fixture = fopen(argv[1], "rb");
    assert(fixture);
    fseek(fixture, 0, SEEK_END);
    size_t size = ftell(fixture);
    rewind(fixture);
    uint8_t* packed = malloc(size);
    assert(fread(packed, 1, size, fixture) == size);
    fclose(fixture);
    for(unsigned scenario = 0; scenario < 8; ++scenario) {
        CompressStreamDecoder decoder = {.input = packed, .input_size = size};
        assert(compress_stream_decoder_rewind(&decoder));
        Storage storage = {0};
        File input = {0};
        TarArchive archive = {
            .storage = &storage, .stream = &input, .unpack_cb = filter, .read_cb = progress};
        CompressedStream stream = {
            .type = CompressTypeGzip, .stream = &input, .decoder = &decoder, .tar = &archive.tar};
        mtar_init(&archive.tar, MTAR_READ, &ops, &stream);
        buffer_allocs = size_calls = outputs = progress_calls = 0;
        fail_open = scenario == 2;
        short_write = scenario == 3;
        fail_mkdir = scenario == 4;
        converting = scenario == 1;
        decoder.fail_at = scenario == 5 ? 1600 : 0;
        bool result;
        if(scenario >= 6) {
            result = tar_archive_unpack_file(
                &archive, scenario == 6 ? "large" : "absent", "/ext/single");
            assert(result == (scenario == 6));
        } else {
            result = tar_archive_unpack_to(&archive, "/ext", converting ? convert : NULL);
            assert(result == (scenario < 2));
            if(result) {
                assert(outputs == 6 && buffer_allocs == 1 && size_calls == 1);
                assert(progress_calls == 7);
                // Rewind and extract again, exercising buffer lifetimes across operations.
                assert(tar_archive_unpack_to(&archive, "/ext", converting ? convert : NULL));
            }
        }
        assert(live_allocs == 1); // Only the fixture survives each operation.
    }
    free(packed);
    assert(live_allocs == 0);
    return 0;
}
