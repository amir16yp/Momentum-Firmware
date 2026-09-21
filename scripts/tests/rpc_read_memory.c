#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <flipper.pb.h>

static size_t live_blocks;
static size_t live_bytes;
static size_t handler_allocations;

typedef union {
    max_align_t alignment;
    size_t size;
} Allocation;

void* tracked_realloc(void* pointer, size_t size) {
    assert(size);
    Allocation* block = pointer ? (Allocation*)pointer - 1 : NULL;
    size_t old_size = block ? block->size : 0;
    block = realloc(block, sizeof(Allocation) + size);
    assert(block);
    if(!pointer) live_blocks++;
    block->size = size;
    live_bytes = live_bytes - old_size + size;
    if(size > old_size) memset((uint8_t*)(block + 1) + old_size, 0, size - old_size);
    return block + 1;
}

void tracked_free(void* pointer) {
    if(!pointer) return;
    Allocation* block = (Allocation*)pointer - 1;
    live_bytes -= block->size;
    live_blocks--;
    free(block);
}

static void* handler_malloc(size_t size) {
    handler_allocations++;
    return tracked_realloc(NULL, size);
}

#define furi_assert(condition) assert(condition)
#define FURI_LOG_D(...)        ((void)0)
#define MIN(a, b)              ((a) < (b) ? (a) : (b))
#define FSAM_READ              1
#define FSOM_OPEN_EXISTING     1
static const size_t MAX_DATA_SIZE = 512;

typedef struct {
    bool connected;
} RpcSession;
typedef struct {
    size_t position;
    bool opened;
} File;
typedef struct {
    RpcSession* session;
    void* api;
} RpcStorageSystem;

static uint8_t input[2601];
static size_t input_size;
static size_t fail_read;
static size_t read_calls;
static bool fail_open;
static bool zero_read;
static size_t file_handles;
static size_t close_calls;
static size_t output_size;
static size_t responses;
static size_t errors;

static File* storage_file_alloc(void* api) {
    assert(api);
    file_handles++;
    return calloc(1, sizeof(File));
}

static bool storage_file_open(File* file, const char* path, int access, int mode) {
    assert(strcmp(path, "/ext/test") == 0);
    assert(access == FSAM_READ && mode == FSOM_OPEN_EXISTING);
    file->opened = !fail_open;
    return file->opened;
}

static size_t storage_file_size(File* file) {
    assert(file->opened);
    return input_size;
}

static uint16_t storage_file_read(File* file, void* output, size_t size) {
    assert(file->opened && size <= 512 && size <= input_size - file->position);
    if(read_calls++ == fail_read) size = zero_read ? 0 : size - 1;
    memcpy(output, input + file->position, size);
    file->position += size;
    return (uint16_t)size;
}

static void storage_file_close(File* file) {
    file->opened = false;
    close_calls++;
}

static void storage_file_free(File* file) {
    assert(!file->opened);
    file_handles--;
    free(file);
}

static PB_CommandStatus rpc_system_storage_get_file_error(File* file) {
    assert(file);
    return PB_CommandStatus_ERROR_STORAGE_INTERNAL;
}

static void
    rpc_system_storage_reset_state(RpcStorageSystem* storage, RpcSession* session, bool error) {
    assert(storage->session == session && error);
}

static void rpc_send(RpcSession* session, PB_Main* message) {
    uint8_t wire[1024];
    pb_ostream_t output = pb_ostream_from_buffer(wire, sizeof(wire));
    assert(pb_encode_ex(&output, &PB_Main_msg, message, PB_ENCODE_DELIMITED));
    if(!session->connected) return;
    PB_Main decoded = PB_Main_init_zero;
    pb_istream_t input_stream = pb_istream_from_buffer(wire, output.bytes_written);
    assert(pb_decode_ex(&input_stream, &PB_Main_msg, &decoded, PB_DECODE_DELIMITED));
    assert(decoded.command_id == 42 && decoded.command_status == PB_CommandStatus_OK);
    assert(decoded.which_content == PB_Main_storage_read_response_tag);
    assert(decoded.content.storage_read_response.has_file);
    const pb_bytes_array_t* data = decoded.content.storage_read_response.file.data;
    size_t size = MIN(input_size - output_size, MAX_DATA_SIZE);
    // Nanopb may omit a zero-length bytes field while preserving has_file.
    assert((data ? data->size : 0) == size);
    if(size) assert(memcmp(data->bytes, input + output_size, size) == 0);
    output_size += size;
    assert(decoded.has_next == (output_size < input_size));
    responses++;
    pb_release(&PB_Main_msg, &decoded);
}

static void rpc_send_and_release(RpcSession* session, PB_Main* response) {
    rpc_send(session, response);
    pb_release(&PB_Main_msg, response);
}

static void rpc_send_and_release_empty(RpcSession* session, uint32_t id, PB_CommandStatus status) {
    assert(id == 42 && status == PB_CommandStatus_ERROR_STORAGE_INTERNAL);
    if(session->connected) errors++;
}

#define malloc handler_malloc
#define free   tracked_free
/* PRODUCTION_CODE */
#undef malloc
#undef free

static void run(size_t size, size_t failure, bool open_failure, bool zero, bool connected) {
    input_size = size;
    fail_read = failure;
    fail_open = open_failure;
    zero_read = zero;
    read_calls = responses = errors = output_size = close_calls = handler_allocations = 0;
    RpcSession session = {.connected = connected};
    RpcStorageSystem storage = {.session = &session, .api = &session};
    PB_Main request = PB_Main_init_zero;
    request.command_id = 42;
    request.which_content = PB_Main_storage_read_request_tag;
    request.content.storage_read_request.path = "/ext/test";
    rpc_system_storage_read_process(&request, &storage);
    assert(live_blocks == 0 && live_bytes == 0 && file_handles == 0);
    assert(close_calls == 1);
    bool failed = open_failure || failure < read_calls;
    if(connected) {
        assert(errors == (failed ? 1U : 0U));
        if(!failed) {
            assert(output_size == size);
            assert(responses == (size ? (size + 511) / 512 : 1));
        } else {
            assert(responses == (open_failure ? 0 : failure));
        }
    }
    assert(handler_allocations == 1 + (open_failure ? 0 : (size ? read_calls : 1)));
}

int main(void) {
    for(size_t i = 0; i < sizeof(input); i++)
        input[i] = (uint8_t)(i * 37 + 11);
    const size_t sizes[] = {0, 1, 511, 512, 513, 1024, 2601};
    for(size_t cycle = 0; cycle < 100; cycle++) {
        for(size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            run(sizes[i], SIZE_MAX, false, false, true);
        }
        run(2601, SIZE_MAX, true, false, true);
        run(2601, SIZE_MAX, false, false, false);
        for(size_t failure = 0; failure < 6; failure++) {
            run(2601, failure, false, false, true);
            run(2601, failure, false, true, true);
        }
    }
    puts("RPC reads: 2100 transfers, protobuf round trips, read failures and cleanup passed");
    return 0;
}
