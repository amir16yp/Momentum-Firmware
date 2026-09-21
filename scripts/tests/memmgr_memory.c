#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* BLOCK_TYPE */
static const size_t xHeapStructSize = sizeof(BlockLink_t);
#define heapBLOCK_ALLOCATED_BITMASK        ((size_t)1 << (sizeof(size_t) * 8 - 1))
#define heapBLOCK_IS_ALLOCATED(block)      ((block)->xBlockSize & heapBLOCK_ALLOCATED_BITMASK)
#define heapPROTECT_BLOCK_POINTER(pointer) (pointer)
#define configASSERT(condition)            assert(condition)

static void* live_blocks[2];
static size_t live_count;
static size_t allocations;
static size_t frees;
static size_t allocation_attempts;
static jmp_buf check_jump;
static int expect_check;

static void checked_condition(int condition) {
    if(condition) return;
    assert(expect_check);
    longjmp(check_jump, 1);
}
#define furi_check(condition) checked_condition(!!(condition))

static void validate_block(BlockLink_t* block) {
    assert(block);
    for(size_t i = 0; i < live_count; i++) {
        if(live_blocks[i] == (uint8_t*)block + xHeapStructSize) return;
    }
    assert(!"unowned block");
}
#define heapVALIDATE_BLOCK_POINTER(block) validate_block(block)

static void* pvPortMalloc(size_t size) {
    allocation_attempts++;
    furi_check(size != 0);
    assert(size < 65536);
    assert(live_count < 2);
    // Exact capacity leaves an ASan redzone immediately after the payload.
    BlockLink_t* block = calloc(1, xHeapStructSize + size);
    assert(block);
    block->xBlockSize = (xHeapStructSize + size) | heapBLOCK_ALLOCATED_BITMASK;
    void* payload = (uint8_t*)block + xHeapStructSize;
    live_blocks[live_count++] = payload;
    allocations++;
    return payload;
}

static void vPortFree(void* data) {
    if(!data) return;
    BlockLink_t* block = (void*)((uint8_t*)data - xHeapStructSize);
    validate_block(block);
    assert(heapBLOCK_IS_ALLOCATED(block));
    for(size_t i = 0; i < live_count; i++) {
        if(live_blocks[i] == data) {
            live_blocks[i] = live_blocks[--live_count];
            break;
        }
    }
    frees++;
    free(block);
}

// Check copy bounds even on hosts without a sanitizer runtime.
static void* checked_memcpy(void* dest, const void* source, size_t size) {
    BlockLink_t* from = (void*)((const uint8_t*)source - xHeapStructSize);
    BlockLink_t* to = (void*)((uint8_t*)dest - xHeapStructSize);
    validate_block(from);
    validate_block(to);
    assert(size <= (from->xBlockSize & ~heapBLOCK_ALLOCATED_BITMASK) - xHeapStructSize);
    assert(size <= (to->xBlockSize & ~heapBLOCK_ALLOCATED_BITMASK) - xHeapStructSize);
    return memcpy(dest, source, size);
}

#define memcpy checked_memcpy
/* REALLOC */
#undef memcpy

struct _reent;
#define UNUSED(value) (void)(value)
/* CALLOC */

#define EXPECT_CHECK(operation)       \
    do {                              \
        expect_check = 1;             \
        if(setjmp(check_jump) == 0) { \
            operation;                \
            assert(!"missing check"); \
        }                             \
        expect_check = 0;             \
    } while(0)

static void test_calloc(void) {
    const size_t overflowing[][2] = {
        {SIZE_MAX, 2},
        {2, SIZE_MAX},
        {SIZE_MAX / 2 + 1, 2},
        {SIZE_MAX / 2 + 2, 2},
        {SIZE_MAX / 3 + 1, 3},
        {SIZE_MAX, SIZE_MAX},
    };
    for(size_t i = 0; i < sizeof(overflowing) / sizeof(overflowing[0]); i++) {
        size_t before = allocation_attempts;
        EXPECT_CHECK(firmware_calloc(overflowing[i][0], overflowing[i][1]));
        EXPECT_CHECK(__wrap__calloc_r(NULL, overflowing[i][0], overflowing[i][1]));
        assert(allocation_attempts == before);
    }
    for(size_t count = 1; count <= 33; count++) {
        for(size_t size = 1; size <= 33; size++) {
            uint8_t* data = __wrap__calloc_r(NULL, count, size);
            for(size_t i = 0; i < count * size; i++)
                assert(data[i] == 0);
            vPortFree(data);
        }
    }
    // Preserve this firmware's existing fail-fast policy for zero-sized malloc.
    size_t before = allocation_attempts;
    EXPECT_CHECK(firmware_calloc(0, SIZE_MAX));
    EXPECT_CHECK(firmware_calloc(SIZE_MAX, 0));
    EXPECT_CHECK(firmware_calloc(0, 0));
    assert(allocation_attempts == before + 3);
    assert(live_count == 0 && allocations == frees);
}

static void test_resize(size_t capacity, size_t new_size) {
    uint8_t* old = pvPortMalloc(capacity);
    for(size_t i = 0; i < capacity; i++)
        old[i] = (uint8_t)(i * 31 + 7);
    size_t before_allocations = allocations;
    size_t before_frees = frees;
    uint8_t* resized = firmware_realloc(old, new_size);
    if(new_size) {
        assert(resized);
        assert(live_count == 1);
        assert(allocations == before_allocations + 1);
        assert(frees == before_frees + 1);
        for(size_t i = 0; i < new_size; i++) {
            assert(resized[i] == (i < capacity ? (uint8_t)(i * 31 + 7) : 0));
        }
        vPortFree(resized);
    } else {
        assert(!resized);
        assert(allocations == before_allocations);
        assert(frees == before_frees + 1);
    }
    assert(live_count == 0);
    assert(allocations == frees);
}

int main(void) {
    test_calloc();
    size_t before = allocations;
    assert(firmware_realloc(NULL, 0) == NULL);
    assert(allocations == before);
    uint8_t* initial = firmware_realloc(NULL, 257);
    for(size_t i = 0; i < 257; i++)
        assert(initial[i] == 0);
    vPortFree(initial);

    const size_t sizes[] = {0, 1, 3, 7, 8, 9, 31, 256, 4096};
    for(size_t cycle = 0; cycle < 100; cycle++) {
        for(size_t i = 1; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            for(size_t j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
                test_resize(sizes[i], sizes[j]);
            }
        }
    }
    assert(live_count == 0 && allocations == frees);
    puts("Memory manager: 7200 realloc lifecycles and calloc overflow/zeroing checks passed");
    return 0;
}
