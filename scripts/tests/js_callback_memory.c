/* Production event dispatch with stable root slots and tracked payload lifetime. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define SYSTEM_ARGS 2
#define MJS_UNDEFINED 0
#define MJS_OK 0
#define ThreadEventStop 1
#define FuriStatusOk 0
#define furi_check assert
#define furi_crash abort

typedef uintptr_t mjs_val_t;
typedef int mjs_err_t;
typedef struct { bool stopped; } FuriEventLoop;
typedef struct { bool taken; } FuriSemaphore;
typedef enum { JsEventLoopObjectTypeSemaphore, JsEventLoopObjectTypeQueue } JsEventLoopObjectType;
struct mjs {
    mjs_val_t* roots[4];
    mjs_val_t returned[2];
    size_t returned_count;
    mjs_val_t expected_payload;
    mjs_err_t error;
    bool stop_requested;
    unsigned calls;
};
typedef struct {
    FuriEventLoop* event_loop;
    JsEventLoopObjectType object_type;
    struct mjs* mjs;
    mjs_val_t callback;
    mjs_val_t* arguments;
    size_t arity;
    mjs_val_t (*transformer)(struct mjs*, void*, void*);
    void* transformer_context;
} JsEventLoopCallbackContext;

static mjs_err_t mjs_apply(struct mjs* mjs, mjs_val_t* result, mjs_val_t callback,
    mjs_val_t this_value, size_t arity, mjs_val_t* args) {
    assert(callback == 99 && this_value == MJS_UNDEFINED && arity == 4);
    for(size_t i = 0; i < arity; i++) assert(mjs->roots[i] == &args[i]);
    assert(args[1] == mjs->expected_payload);
    mjs->calls++;
    *result = 100;
    return mjs->error;
}
static size_t mjs_array_length(struct mjs* mjs, mjs_val_t result) {
    assert(result == 100);
    return mjs->returned_count;
}
static mjs_val_t mjs_array_get(struct mjs* mjs, mjs_val_t result, size_t index) {
    assert(result == 100 && index < mjs->returned_count);
    return mjs->returned[index];
}
static unsigned js_flags_wait(struct mjs* mjs, unsigned flags, unsigned timeout) {
    assert(flags == ThreadEventStop && timeout == 0);
    return mjs->stop_requested ? ThreadEventStop : 0;
}
static void furi_event_loop_stop(FuriEventLoop* loop) { loop->stopped = true; }
static int furi_semaphore_acquire(FuriSemaphore* sem, unsigned timeout) {
    assert(timeout == 0);
    sem->taken = true;
    return FuriStatusOk;
}
static mjs_val_t transform(struct mjs* mjs, void* object, void* context) {
    assert(object == context);
    /* The payload slot stays registered even while the transformer runs. */
    assert(mjs->roots[1] && *mjs->roots[1] == MJS_UNDEFINED);
    return mjs->expected_payload;
}
/* PRODUCTION_CODE */

int main(void) {
    struct mjs mjs = {.returned = {7, 8}, .returned_count = 2, .expected_payload = 12345};
    FuriEventLoop loop = {0};
    mjs_val_t args[] = {1, MJS_UNDEFINED, 3, 4};
    for(size_t i = 0; i < 4; i++) mjs.roots[i] = &args[i];
    JsEventLoopCallbackContext context = {
        .event_loop = &loop, .object_type = JsEventLoopObjectTypeQueue,
        .mjs = &mjs, .callback = 99, .arguments = args, .arity = 4,
        .transformer = transform, .transformer_context = &mjs,
    };
    for(unsigned i = 0; i < 1000; i++) {
        js_event_loop_callback(&mjs, &context);
        assert(args[1] == MJS_UNDEFINED && args[2] == 7 && args[3] == 8);
        assert(!loop.stopped);
    }
    /* A script can deliberately retain the event in its returned state. */
    mjs.returned[0] = mjs.expected_payload;
    js_event_loop_callback(&mjs, &context);
    assert(args[1] == MJS_UNDEFINED && args[2] == mjs.expected_payload);
    mjs.returned_count = 0;
    js_event_loop_callback(&mjs, &context);
    assert(args[1] == MJS_UNDEFINED && args[2] == mjs.expected_payload);
    mjs.error = 2;
    js_event_loop_callback(&mjs, &context);
    assert(loop.stopped && args[1] == MJS_UNDEFINED);
    mjs.error = 0;
    loop.stopped = false;
    mjs.stop_requested = true;
    js_event_loop_callback(&mjs, &context);
    assert(loop.stopped && args[1] == MJS_UNDEFINED);
    /* Semaphore and timer callbacks have no payload. */
    context.transformer = NULL;
    context.object_type = JsEventLoopObjectTypeSemaphore;
    mjs.expected_payload = MJS_UNDEFINED;
    mjs.stop_requested = false;
    loop.stopped = false;
    FuriSemaphore semaphore = {0};
    js_event_loop_callback(&semaphore, &context);
    assert(semaphore.taken && !loop.stopped);
    js_event_loop_callback_generic(&context);
    assert(args[1] == MJS_UNDEFINED && !loop.stopped);
    return 0;
}
