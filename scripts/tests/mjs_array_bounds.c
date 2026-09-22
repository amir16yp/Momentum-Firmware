#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

typedef int64_t mjs_val_t;
typedef int mjs_err_t;
#define MJS_UNDEFINED  INT64_MIN
#define MJS_OK         0
#define MJS_TYPE_ERROR 1
struct mjs {
    char* data;
    size_t size;
    int type;
};
static mjs_val_t mjs_get(struct mjs* mjs, mjs_val_t object, const char* key, int len) {
    (void)object;
    (void)len;
    return strcmp(key, "_t") == 0 ? mjs->type : 0;
}
static char* mjs_array_buf_get_ptr(struct mjs* mjs, mjs_val_t object, size_t* length) {
    (void)object;
    *length = mjs->size;
    return mjs->data;
}
static int mjs_get_int(struct mjs* mjs, mjs_val_t value) {
    (void)mjs;
    return (int)value;
}
static double mjs_get_double(struct mjs* mjs, mjs_val_t value) {
    (void)mjs;
    return (double)value;
}
static mjs_val_t mjs_mk_number(struct mjs* mjs, int64_t value) {
    (void)mjs;
    return value;
}
static bool mjs_is_number(mjs_val_t value) {
    return value != MJS_UNDEFINED;
}
static bool mjs_is_boolean(mjs_val_t value) {
    (void)value;
    return false;
}
static bool mjs_get_bool(struct mjs* mjs, mjs_val_t value) {
    (void)mjs;
    return value != 0;
}
/* PRODUCTION_CODE */

int main(void) {
    uint8_t* data = malloc(64);
    assert(data);
    for(int type = MJS_DATAVIEW_U8; type <= MJS_DATAVIEW_I32; type++) {
        const size_t width = mjs_dataview_get_element_len(type);
        for(size_t size = 0; size <= 64; size++) {
            memset(data, 0x5a, 64);
            struct mjs mjs = {.data = (char*)data, .size = size, .type = type};
            for(size_t index = 0; index < size / width; index++) {
                assert(mjs_dataview_set_prop(&mjs, 0, index, 42) == MJS_OK);
                assert(mjs_dataview_get_prop(&mjs, 0, index) == 42);
            }
            uint8_t before[64];
            memcpy(before, data, 64);
            size_t invalid[] = {
                size / width,
                SIZE_MAX,
                SIZE_MAX - 1,
                SIZE_MAX / width,
                SIZE_MAX / width + (width > 1),
                (size_t)INT_MAX};
            for(size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
                assert(mjs_dataview_get(&mjs, 0, invalid[i]) == MJS_UNDEFINED);
                assert(mjs_dataview_set(&mjs, 0, invalid[i], 99) == MJS_TYPE_ERROR);
                assert(memcmp(before, data, 64) == 0);
            }
            assert(mjs_dataview_get_prop(&mjs, 0, -1) == MJS_UNDEFINED);
            assert(mjs_dataview_set_prop(&mjs, 0, -1, 99) == MJS_TYPE_ERROR);
            assert(memcmp(before, data, 64) == 0);
            mjs.data = NULL;
            assert(mjs_dataview_get(&mjs, 0, 0) == MJS_UNDEFINED);
            assert(mjs_dataview_set(&mjs, 0, 0, 99) == MJS_TYPE_ERROR);
        }
    }
    free(data);
    puts(
        "mJS: all six element types, partial tails, negative/overflow indices and null buffers passed");
    return 0;
}
