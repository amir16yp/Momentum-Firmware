#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint64_t mjs_val_t;
typedef struct {
    mjs_val_t* slots[8];
    size_t capacity;
    size_t count;
    size_t head;
    bool fail_put;
} FuriMessageQueue;
typedef FuriMessageQueue FuriEventLoopObject;
typedef struct {
    FuriMessageQueue* object;
} JsEventLoopContract;
struct mjs {
    mjs_val_t argument;
    JsEventLoopContract* context;
    mjs_val_t* roots[16];
    size_t root_count;
    bool destroyed;
};

static size_t live_nodes;
static void* node_alloc(size_t size) {
    assert(size == sizeof(mjs_val_t));
    void* node = malloc(size);
    assert(node);
    live_nodes++;
    return node;
}
static void node_free(void* node) {
    assert(node && live_nodes);
    live_nodes--;
    free(node);
}

static void mjs_own(struct mjs* mjs, mjs_val_t* value) {
    assert(!mjs->destroyed && mjs->root_count < 16);
    mjs->roots[mjs->root_count++] = value;
}
static void mjs_disown(struct mjs* mjs, mjs_val_t* value) {
    assert(!mjs->destroyed);
    for(size_t i = 0; i < mjs->root_count; i++) {
        if(mjs->roots[i] == value) {
            mjs->roots[i] = mjs->roots[--mjs->root_count];
            return;
        }
    }
    assert(!"missing GC root");
}
#define MJS_UNDEFINED UINT64_MAX
static void mjs_return(struct mjs* mjs, mjs_val_t value) {
    assert(!mjs->destroyed && value == MJS_UNDEFINED);
}

enum {
    FuriStatusOk,
    FuriStatusErrorResource
};
static int furi_message_queue_put(FuriMessageQueue* queue, mjs_val_t** value, int timeout) {
    assert(timeout == 0);
    if(queue->fail_put || queue->count == queue->capacity) return FuriStatusErrorResource;
    queue->slots[(queue->head + queue->count++) % queue->capacity] = *value;
    return FuriStatusOk;
}
static int furi_message_queue_get(FuriMessageQueue* queue, mjs_val_t** value, int timeout) {
    assert(timeout == 0);
    if(!queue->count) return FuriStatusErrorResource;
    *value = queue->slots[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return FuriStatusOk;
}

typedef int JsValueDeclaration;
typedef struct {
    const JsValueDeclaration* declarations;
} JsValueArguments;
#define JsValueTypeAny         1
#define JS_VALUE_SIMPLE(value) value
#define JS_VALUE_ARGS(list)    {list}
#define JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, args, output)   \
    do {                                                   \
        assert((args)->declarations[0] == JsValueTypeAny); \
        *(output) = (mjs)->argument;                       \
    } while(0)
#define JS_GET_CONTEXT(mjs) ((mjs)->context)
#define UNUSED(value)       (void)(value)
#define furi_check(value)   assert(value)
#define malloc              node_alloc
#define free                node_free
/* PRODUCTION_CODE */
#undef malloc
#undef free

static void check_roots(struct mjs* mjs, FuriMessageQueue* queue) {
    assert(mjs->root_count == queue->count && live_nodes == queue->count);
    // A GC scan must only see the still-live queued nodes.
    for(size_t i = 0; i < mjs->root_count; i++) {
        assert(*mjs->roots[i] < queue->capacity);
    }
}

static void run(size_t capacity) {
    FuriMessageQueue queue = {.capacity = capacity};
    JsEventLoopContract contract = {.object = &queue};
    struct mjs mjs = {.context = &contract};
    for(size_t round = 0; round < 10; round++) {
        for(size_t i = 0; i < capacity; i++) {
            mjs.argument = i;
            js_event_loop_queue_send(&mjs);
            check_roots(&mjs, &queue);
        }
        for(size_t i = 0; i < 100; i++) {
            mjs.argument = 1000 + i;
            js_event_loop_queue_send(&mjs);
            check_roots(&mjs, &queue);
        }
        for(size_t i = 0; i < capacity; i++) {
            assert(js_event_loop_queue_transformer(&mjs, &queue, NULL) == i);
            check_roots(&mjs, &queue);
        }
    }
    queue.fail_put = true;
    js_event_loop_queue_send(&mjs);
    check_roots(&mjs, &queue);
    assert(live_nodes == 0 && mjs.root_count == 0);
}

int main(void) {
    for(size_t cycle = 0; cycle < 100; cycle++) {
        run(1);
        run(2);
        run(8);
    }
    puts("JS queue: 300 lifecycles, 300000 rejected sends, FIFO and GC-root cleanup passed");
    return 0;
}
