#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

typedef int64_t mjs_val_t;
#define MJS_UNDEFINED      INT64_MIN
#define ARRAY              (INT64_MIN + 1)
#define TYPED              (INT64_MIN + 2)
#define VIEW               (INT64_MIN + 3)
#define RESULT             (INT64_MIN + 4)
#define MJS_BAD_ARGS_ERROR 1
struct mjs {
    mjs_val_t args[4];
    size_t nargs;
    bool error;
    mjs_val_t result;
    size_t copied;
    uint8_t tx[2];
};
static size_t live_allocations;
static size_t allocation_calls;
static size_t bus_calls;
static int bus_locks;
static bool fail_bus;

static void* checked_alloc(size_t size) {
    // Unexpected huge requests fail here instead of exhausting the host.
    assert(size && size <= 32);
    live_allocations++;
    allocation_calls++;
    void* data = malloc(size);
    assert(data);
    return data;
}
static void checked_free(void* data) {
    assert(data && live_allocations);
    live_allocations--;
    free(data);
}
static void mjs_prepend_errorf(struct mjs* mjs, int error, const char* format, ...) {
    (void)format;
    assert(error == MJS_BAD_ARGS_ERROR);
    mjs->error = true;
}
static void mjs_return(struct mjs* mjs, mjs_val_t result) {
    mjs->result = result;
}
static size_t mjs_nargs(struct mjs* mjs) {
    return mjs->nargs;
}
static mjs_val_t mjs_arg(struct mjs* mjs, size_t index) {
    assert(index < mjs->nargs);
    return mjs->args[index];
}
static bool mjs_is_number(mjs_val_t value) {
    return value >= INT32_MIN && value <= INT32_MAX;
}
static int32_t mjs_get_int32(struct mjs* mjs, mjs_val_t value) {
    (void)mjs;
    return (int32_t)value;
}
static bool mjs_is_array(mjs_val_t value) {
    return value == ARRAY;
}
static size_t mjs_array_length(struct mjs* mjs, mjs_val_t value) {
    (void)mjs;
    assert(value == ARRAY);
    return 2;
}
static mjs_val_t mjs_array_get(struct mjs* mjs, mjs_val_t value, size_t index) {
    assert(value == ARRAY && index < 2);
    return mjs->tx[index];
}
static bool mjs_is_typed_array(mjs_val_t value) {
    return value == TYPED || value == VIEW;
}
static bool mjs_is_data_view(mjs_val_t value) {
    return value == VIEW;
}
static mjs_val_t mjs_dataview_get_buf(struct mjs* mjs, mjs_val_t value) {
    (void)mjs;
    assert(value == VIEW);
    return TYPED;
}
static char* mjs_array_buf_get_ptr(struct mjs* mjs, mjs_val_t value, size_t* length) {
    assert(value == TYPED);
    *length = 2;
    return (char*)mjs->tx;
}
static mjs_val_t mjs_mk_array_buf(struct mjs* mjs, char* data, size_t length) {
    for(size_t i = 0; i < length; i++)
        assert((uint8_t)data[i] == (uint8_t)(i + 0x80));
    mjs->copied = length;
    return RESULT;
}
static const int furi_hal_i2c_handle_external;
static void furi_hal_i2c_acquire(const int* handle) {
    assert(handle == &furi_hal_i2c_handle_external && bus_locks == 0);
    bus_locks++;
}
static void furi_hal_i2c_release(const int* handle) {
    assert(handle == &furi_hal_i2c_handle_external && bus_locks == 1);
    bus_locks--;
}
static bool furi_hal_i2c_rx(
    const int* handle,
    uint32_t address,
    uint8_t* data,
    size_t length,
    uint32_t timeout) {
    assert(handle == &furi_hal_i2c_handle_external && address == 42 && timeout == 1);
    assert(bus_locks == 1 && length > 0 && length <= 32);
    bus_calls++;
    for(size_t i = 0; i < length; i++)
        data[i] = (uint8_t)(i + 0x80);
    return !fail_bus;
}
static bool furi_hal_i2c_trx(
    const int* handle,
    uint32_t address,
    const uint8_t* tx,
    size_t tx_length,
    uint8_t* rx,
    size_t rx_length,
    uint32_t timeout) {
    assert(tx_length == 2 && tx[0] == 11 && tx[1] == 22);
    return furi_hal_i2c_rx(handle, address, rx, rx_length, timeout);
}
#define malloc checked_alloc
#define free   checked_free
/* PRODUCTION_CODE */
#undef malloc
#undef free

static void run(int32_t length, mjs_val_t tx_type, bool invalid_timeout) {
    struct mjs mjs = {.tx = {11, 22}};
    bool combined = tx_type != 0;
    mjs.args[0] = 42;
    mjs.nargs = combined ? 3 : 2;
    if(combined) mjs.args[1] = tx_type;
    mjs.args[combined ? 2 : 1] = length;
    if(invalid_timeout) mjs.args[mjs.nargs++] = MJS_UNDEFINED;
    bus_calls = allocation_calls = 0;
    if(combined)
        js_i2c_write_read(&mjs);
    else
        js_i2c_read(&mjs);
    assert(live_allocations == 0 && bus_locks == 0);
    if(length <= 0 || invalid_timeout) {
        assert(mjs.error && mjs.result == MJS_UNDEFINED && bus_calls == 0);
        if(length <= 0) assert(allocation_calls == (tx_type == ARRAY ? 1U : 0U));
    } else {
        assert(!mjs.error && bus_calls == 1);
        assert(mjs.result == (fail_bus ? MJS_UNDEFINED : RESULT));
        assert(mjs.copied == (fail_bus ? 0U : (size_t)length));
    }
}
int main(void) {
    const int32_t lengths[] = {INT32_MIN, -100, -1, 0, 1, 32};
    const mjs_val_t types[] = {0, ARRAY, TYPED, VIEW};
    for(size_t cycle = 0; cycle < 100; cycle++) {
        for(size_t t = 0; t < 4; t++) {
            for(size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
                run(lengths[i], types[t], false);
            run(32, types[t], true);
            fail_bus = true;
            run(32, types[t], false);
            fail_bus = false;
        }
    }
    puts("JS I2C: signed lengths, transmit ownership, timeout and bus-failure cleanup passed");
    return 0;
}
