#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocations;
static size_t live_allocations;
static size_t live_bytes;
static jmp_buf check_jump;
static int expect_check;

static void check(int condition) {
    if(condition) return;
    assert(expect_check);
    longjmp(check_jump, 1);
}

// Firmware allocations are zero-filled. Track ownership and a trailing canary.
static void* tracked_malloc(size_t size) {
    size_t* allocation = calloc(1, sizeof(size_t) + size + 16);
    assert(allocation);
    *allocation = size;
    unsigned char* data = (unsigned char*)(allocation + 1);
    memset(data + size, 0xa5, 16);
    allocations++;
    live_allocations++;
    live_bytes += size;
    return data;
}

static void tracked_free(void* data) {
    size_t* allocation = (size_t*)data - 1;
    for(size_t i = 0; i < 16; i++) {
        assert(((unsigned char*)data)[*allocation + i] == 0xa5);
    }
    live_allocations--;
    live_bytes -= *allocation;
    free(allocation);
}

#define furi_check(condition) check(!!(condition))
#define FURI_BIT(value, bit)  (((value) >> (bit)) & 1U)
#define malloc                tracked_malloc
#define free                  tracked_free
/* PRODUCTION_CODE */
#undef malloc
#undef free

#define EXPECT_CHECK(operation)       \
    do {                              \
        expect_check = 1;             \
        if(setjmp(check_jump) == 0) { \
            operation;                \
            assert(!"missing check"); \
        }                             \
        expect_check = 0;             \
    } while(0)

static void test_capacity(size_t capacity) {
    size_t previous = allocations;
    BitBuffer* buffer = bit_buffer_alloc(capacity);
    assert(allocations == previous + 1);
    assert(live_allocations == 1);
    assert(live_bytes == sizeof(BitBuffer) + capacity + (capacity + 7) / 8);
    assert(bit_buffer_get_capacity_bytes(buffer) == capacity);
    assert(bit_buffer_get_size(buffer) == 0);
    uint8_t* short_input = malloc(1);
    assert(short_input);
    *short_input = 0xab;
    bit_buffer_copy_bytes_with_parity(buffer, short_input + 1, 0);
    assert(bit_buffer_get_size(buffer) == 0);
    for(size_t bits = 1; bits <= 8; bits++) {
        bit_buffer_copy_bytes_with_parity(buffer, short_input, bits);
        assert(bit_buffer_get_size(buffer) == bits);
        assert(bit_buffer_get_byte_from_bit(buffer, 0) == (0xab & ((1U << bits) - 1)));
    }
    free(short_input);
    bit_buffer_reset(buffer);
    for(size_t i = 0; i < capacity; i++)
        bit_buffer_append_byte(buffer, (uint8_t)(i * 37 + 0xab));

    // A nonzero parity region must never leak into a read at the data boundary.
    memset(buffer->parity, 0xff, (capacity + 7) / 8);
    for(size_t tail = 0; tail < 8; tail++) {
        size_t bits = capacity * 8 - tail;
        bit_buffer_set_size(buffer, bits);
        for(size_t offset = 0; offset < bits; offset++) {
            uint8_t expected = 0;
            for(size_t bit = 0; bit < 8 && offset + bit < bits; bit++) {
                expected |= FURI_BIT(buffer->data[(offset + bit) / 8], (offset + bit) % 8) << bit;
            }
            assert(bit_buffer_get_byte_from_bit(buffer, offset) == expected);
        }
    }
    bit_buffer_reset(buffer);
    for(size_t i = 0; i < capacity; i++)
        assert(buffer->data[i] == 0);
    for(size_t i = 0; i < (capacity + 7) / 8; i++)
        assert(buffer->parity[i] == 0);
    EXPECT_CHECK(bit_buffer_get_byte_from_bit(buffer, 0));

    // Encode independently, including every parity alignment and exact capacity.
    uint8_t* encoded = calloc((capacity * 9 + 7) / 8, 1);
    assert(encoded);
    for(size_t i = 0; i < capacity; i++) {
        uint16_t word = (uint8_t)(i * 37 + 0xab) | ((i & 1) << 8);
        for(size_t bit = 0; bit < 9; bit++) {
            encoded[(i * 9 + bit) / 8] |= FURI_BIT(word, bit) << ((i * 9 + bit) % 8);
        }
    }
    bit_buffer_copy_bytes_with_parity(buffer, encoded, capacity * 9);
    assert(bit_buffer_get_size(buffer) == capacity * 8);
    for(size_t i = 0; i < capacity; i++) {
        assert(buffer->data[i] == (uint8_t)(i * 37 + 0xab));
        assert(FURI_BIT(buffer->parity[i / 8], i % 8) == (i & 1));
    }
    EXPECT_CHECK(bit_buffer_copy_bytes_with_parity(buffer, encoded, (capacity + 1) * 9));
    EXPECT_CHECK(bit_buffer_copy_bytes_with_parity(buffer, encoded, 10));
    uint8_t output = 0;
    EXPECT_CHECK(bit_buffer_write_bytes_mid(buffer, &output, SIZE_MAX, 2));
    EXPECT_CHECK(bit_buffer_write_bytes_mid(buffer, &output, 1, SIZE_MAX));
    bit_buffer_write_bytes_mid(buffer, &output, capacity - 1, 1);
    assert(output == (uint8_t)((capacity - 1) * 37 + 0xab));
    bit_buffer_write_bytes_mid(buffer, &output, capacity, 0);
    free(encoded);
    bit_buffer_free(buffer);
    assert(live_allocations == 0 && live_bytes == 0);
}

int main(void) {
    BitBuffer* buffer = bit_buffer_alloc(8192);
    uint8_t input[] = {0xff, 0x12, 0x34};
    bit_buffer_copy_bytes(buffer, input, sizeof(input));
    bit_buffer_set_size(buffer, 0);
    bit_buffer_append_bit(buffer, false);
    assert(bit_buffer_get_byte_from_bit(buffer, 0) == 0);
    bit_buffer_copy_bytes(buffer, input, sizeof(input));
    bit_buffer_copy_right(buffer, buffer, 1);
    assert(bit_buffer_get_size_bytes(buffer) == 2);
    assert(bit_buffer_get_byte(buffer, 0) == 0x12 && bit_buffer_get_byte(buffer, 1) == 0x34);
    bit_buffer_copy_left(buffer, buffer, 1);
    assert(bit_buffer_get_size_bytes(buffer) == 1 && bit_buffer_get_byte(buffer, 0) == 0x12);
    EXPECT_CHECK(bit_buffer_append_right(buffer, buffer, SIZE_MAX));
    EXPECT_CHECK(bit_buffer_append_bytes(buffer, input, SIZE_MAX));
    bit_buffer_reset(buffer);
    bit_buffer_set_size_bytes(buffer, 8192);
    // More than 65535 encoded bits and an exact output size divisible by eight.
    const size_t encoded_size = 8192 * 9 / 8;
    uint8_t* encoded = calloc(encoded_size, 1);
    size_t written = 0;
    bit_buffer_write_bytes_with_parity(buffer, encoded, encoded_size, &written);
    assert(written == 8192 * 9);
    free(encoded);
    bit_buffer_free(buffer);
    EXPECT_CHECK(bit_buffer_alloc(0));
    EXPECT_CHECK(bit_buffer_alloc(SIZE_MAX));
    EXPECT_CHECK(bit_buffer_alloc(SIZE_MAX / 8 + 1));
    const size_t capacities[] = {1, 2, 7, 8, 9, 32, 255, 256, 512};
    for(size_t cycle = 0; cycle < 100; cycle++) {
        for(size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); i++) {
            test_capacity(capacities[i]);
        }
    }
    puts("BitBuffer: 900 lifecycles, allocation counts, boundaries and canaries passed");
    return 0;
}
