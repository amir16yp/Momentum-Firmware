#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define furi_check(value)       assert(value)
#define furi_assert(value)      assert(value)
#define FURI_LOG_T(...)         ((void)0)
#define STREAM_BUFFER_SIZE      32U
#define StreamOffsetFromCurrent 1

typedef struct {
    const uint8_t* data;
    size_t size;
    size_t position;
    size_t read_limit;
} Stream;

static size_t stream_read(Stream* stream, uint8_t* data, size_t size) {
    if(size > stream->read_limit) size = stream->read_limit;
    if(size > stream->size - stream->position) size = stream->size - stream->position;
    memcpy(data, stream->data + stream->position, size);
    stream->position += size;
    return size;
}

static bool stream_seek(Stream* stream, int32_t offset, int origin) {
    assert(origin == StreamOffsetFromCurrent);
    int64_t position = (int64_t)stream->position + offset;
    if(position < 0 || (uint64_t)position > stream->size) return false;
    stream->position = (size_t)position;
    return true;
}

typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} FuriString;
static size_t live_strings;
static size_t peak_capacity;

static FuriString* furi_string_alloc(void) {
    FuriString* string = calloc(1, sizeof(FuriString));
    assert(string);
    string->data = calloc(1, 1);
    assert(string->data);
    string->capacity = 1;
    live_strings++;
    return string;
}

static void furi_string_free(FuriString* string) {
    free(string->data);
    free(string);
    live_strings--;
}

static void furi_string_reset(FuriString* string) {
    string->size = 0;
    string->data[0] = 0;
}

static void furi_string_push_back(FuriString* string, char value) {
    if(string->size + 1 >= string->capacity) {
        string->capacity *= 2;
        string->data = realloc(string->data, string->capacity);
        assert(string->data);
        if(string->capacity > peak_capacity) peak_capacity = string->capacity;
    }
    string->data[string->size++] = value;
    string->data[string->size] = 0;
}

static size_t furi_string_size(const FuriString* string) {
    return string->size;
}

static char furi_string_get_char(const FuriString* string, size_t index) {
    assert(index < string->size);
    return string->data[index];
}

static void furi_string_left(FuriString* string, size_t size) {
    if(string->size > size) {
        string->size = size;
        string->data[size] = 0;
    }
}

typedef struct KeysDict KeysDict;
/* PRODUCTION_CODE */

static const uint8_t expected_key[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab};

static void verify(const uint8_t* input, size_t length, size_t read_limit, size_t expected_count) {
    Stream stream = {input, length, 0, read_limit};
    KeysDict dict = {.stream = &stream, .key_size = 6, .key_size_symbols = 13};
    uint8_t output[6];
    size_t count = 0;
    while(true) {
        memset(output, 0x5a, sizeof(output));
        if(!keys_dict_get_next_key(&dict, output, sizeof(output))) {
            for(size_t i = 0; i < sizeof(output); i++)
                assert(output[i] == 0x5a);
            break;
        }
        assert(memcmp(output, expected_key, sizeof(output)) == 0);
        count++;
    }
    assert(count == expected_count);
    assert(stream.position == stream.size);
    assert(live_strings == 0);

    // The same reader is used to count entries during dictionary startup.
    stream.position = 0;
    FuriString* line = furi_string_alloc();
    bool eof = false;
    count = 0;
    while(!eof) {
        if(keys_dict_read_key_line(&dict, line, &eof)) count++;
    }
    assert(count == expected_count);
    furi_string_free(line);
    assert(live_strings == 0);
}

int main(void) {
    const char input[] = "# dictionary\r\n\n\r\ninvalidhex!!\n0123456789A\n0123456789AB\r\n"
                         "0123456789ab ignored suffix\n0123\r456789aB\n0123456789AB";
    for(size_t cycle = 0; cycle < 100; cycle++) {
        for(size_t limit = 1; limit <= 32; limit++) {
            verify((const uint8_t*)input, sizeof(input) - 1, limit, 4);
        }
    }
    verify((const uint8_t*)"", 0, 32, 0);
    verify((const uint8_t*)"\r\r", 2, 32, 0);
    // Reject invalid hex at every position, even when it is a NUL byte.
    const char invalid[] = {'G', ' ', '\0', '/', ':'};
    for(size_t i = 0; i < 12; i++) {
        for(size_t j = 0; j < sizeof(invalid); j++) {
            char lines[] = "0123456789AB\n0123456789AB\n";
            lines[i] = invalid[j];
            verify((const uint8_t*)lines, sizeof(lines) - 1, 7, 1);
        }
    }
    // One-megabyte comment and valid-key suffix must not hide the next key.
    const size_t long_size = 1024 * 1024;
    uint8_t* long_line = malloc(long_size + 14);
    assert(long_line);
    memset(long_line, 'x', long_size);
    long_line[0] = '#';
    memcpy(long_line + long_size, "\n0123456789AB\n", 14);
    peak_capacity = 0;
    verify(long_line, long_size + 14, 32, 1);
    memcpy(long_line, "0123456789AB", 12);
    verify(long_line, long_size + 14, 32, 2);
    free(long_line);
    printf(
        "Keys dictionary: malformed input, short reads and cleanup passed; peak string capacity %zu\n",
        peak_capacity);
    return 0;
}
