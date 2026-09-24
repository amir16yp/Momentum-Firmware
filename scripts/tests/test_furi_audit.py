"""Run audited production functions against small, deterministic host RTOS stubs.

HOT_PATH_ASAN=1 enables AddressSanitizer. OPTIMIZATION_BASELINE_REV selects
an older revision to verify that these tests catch the original defects.
"""
import unittest
import re

from test_hot_path_optimization import run_c, source


def function(path, name):
    code = source("furi/" + path)
    match = re.search(r"^[A-Za-z_][\w* \t]*(?:\n[ \t]+)?" + name +
                      r"\([^;{}]*\)\s*\{", code, re.MULTILINE)
    if match is None:
        raise ValueError(f"Definition not found: {name}")
    start = match.start()
    brace = match.end() - 1
    depth = 1
    end = brace + 1
    while depth:
        depth += (code[end] == "{") - (code[end] == "}")
        end += 1
    return code[start:end] + "\n"


PREAMBLE = r"""
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define furi_assert(x) assert(x)
#define furi_check(x) assert(x)
#define FuriWaitForever UINT32_MAX
#define FuriStatusErrorISR (-6)
#define FuriStatusErrorParameter (-4)
#define FuriStatusErrorResource (-3)
#define FuriStatusErrorTimeout (-2)
#define FuriStatusError (-1)
#define FuriFlagWaitAll 1
#define FuriFlagNoClear 2
#define pdFALSE 0
#define pdFAIL 0
#define pdPASS 1
#define pdTRUE 1
typedef int BaseType_t;
typedef uint32_t TickType_t;
"""


class FuriAuditTest(unittest.TestCase):
    def test_heap_calloc(self):
        code = PREAMBLE + r"""
#define heapMULTIPLY_WILL_OVERFLOW(a,b) ((a) && (b) > SIZE_MAX / (a))
static unsigned allocations, redundant_clears;
static void* pvPortMalloc(size_t size) { ++allocations; return calloc(1, size); }
static void* counted_memset(void* p, int value, size_t size) {
    ++redundant_clears; return memset(p, value, size);
}
#define memset counted_memset
"""
        code += function("core/memmgr_heap.c", "pvPortCalloc")
        code += r"""
int main(void) {
    assert(pvPortCalloc(SIZE_MAX, 2) == NULL && allocations == 0);
    for(size_t size = 1; size <= 256; ++size) {
        unsigned char* p = pvPortCalloc(3, size);
        for(size_t i = 0; i < 3 * size; ++i) assert(p[i] == 0);
        free(p);
    }
    assert(allocations == 256 && redundant_clears == 0);
}
"""
        run_c(code)

    def test_invalid_thread_flags(self):
        code = PREAMBLE + r"""
typedef void* FuriThreadId;
typedef void* TaskHandle_t;
static bool isr;
#define FURI_IS_IRQ_MODE() isr
#define THREAD_FLAGS_INVALID_BITS 0x80000000U
#define THREAD_NOTIFY_INDEX 1
#define eSetBits 1
#define eNoAction 0
#define portYIELD_FROM_ISR(x) ((void)(x))
static unsigned notifications, callbacks;
static uint32_t bits;
static int xTaskNotifyIndexed(void* task, unsigned index, uint32_t flags, int action) {
    assert(task && index == 1 && action == eSetBits);
    ++notifications; bits |= flags; return 1;
}
static int xTaskNotifyAndQueryIndexed(void* task, unsigned index, uint32_t flags,
                                      int action, uint32_t* result) {
    assert(task && index == 1 && flags == 0 && action == eNoAction);
    *result = bits; return 1;
}
static int xTaskNotifyIndexedFromISR(void* t, unsigned i, uint32_t f, int a, int* yield) {
    (void)yield; return xTaskNotifyIndexed(t, i, f, a);
}
static int xTaskNotifyAndQueryIndexedFromISR(void* t, unsigned i, uint32_t f, int a,
                                             uint32_t* r, int* yield) {
    (void)yield; return xTaskNotifyAndQueryIndexed(t, i, f, a, r);
}
static void furi_event_loop_thread_flag_callback(void* task) { assert(task); ++callbacks; }
"""
        code += function("core/thread.c", "furi_thread_flags_set")
        code += r"""
int main(void) {
    for(unsigned mode = 0; mode < 2; ++mode) {
        isr = mode; notifications = callbacks = 0; bits = 0;
        assert(furi_thread_flags_set(NULL, 1) == (uint32_t)FuriStatusErrorParameter);
        assert(furi_thread_flags_set((void*)1, 0x80000000U) == (uint32_t)FuriStatusErrorParameter);
        assert(notifications == 0 && callbacks == 0);
        assert(furi_thread_flags_set((void*)1, 3) == 3);
        assert(notifications == 1 && callbacks == 1);
    }
}
"""
        run_c(code)

    def test_pending_and_thread_notifications(self):
        code = PREAMBLE + r"""
typedef void* TaskHandle_t;
typedef void FuriThread;
typedef bool (*SignalCallback)(uint32_t, void*, void*);
enum { FuriEventLoopStateStopped, FuriEventLoopStateRunning };
enum { FuriEventLoopFlagEvent=1, FuriEventLoopFlagStop=2, FuriEventLoopFlagTimer=4,
       FuriEventLoopFlagPending=8, FuriEventLoopFlagThreadFlag=16, FuriEventLoopFlagAll=31 };
#define FURI_EVENT_LOOP_FLAG_NOTIFY_INDEX 2
#define eSetBits 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define furi_crash() abort()
typedef struct {
    void* thread_id; int state; bool are_thread_flags_subscribed;
    void (*thread_flags_callback)(void*); void* thread_flags_callback_context;
} FuriEventLoop;
static uint32_t notifications;
static unsigned waits, pending_calls, thread_calls;
static void* furi_thread_get_current_id(void) { return (void*)1; }
static FuriThread* furi_thread_get_current(void) { return (void*)1; }
static bool furi_event_loop_signal_callback(uint32_t signal, void* arg, void* ctx) {
    (void)signal; (void)arg; (void)ctx; return false;
}
static SignalCallback furi_thread_get_signal_callback(FuriThread* t) { (void)t; return NULL; }
static void furi_thread_set_signal_callback(FuriThread* t, SignalCallback cb, void* ctx) {
    (void)t; (void)cb; (void)ctx;
}
static void furi_event_loop_init_tick(FuriEventLoop* loop) { (void)loop; }
static uint32_t furi_event_loop_get_timer_wait_time(FuriEventLoop* l) { (void)l; return 1; }
static uint32_t furi_event_loop_get_tick_wait_time(FuriEventLoop* l) { (void)l; return 1; }
static void furi_event_loop_process_waiting_list(FuriEventLoop* l) { (void)l; }
static void furi_event_loop_process_timer_queue(FuriEventLoop* l) { (void)l; }
static void furi_event_loop_process_pending_callbacks(FuriEventLoop* l) { (void)l; ++pending_calls; }
static bool furi_event_loop_process_expired_timers(FuriEventLoop* l) { (void)l; return false; }
static void furi_event_loop_process_tick(FuriEventLoop* l) { (void)l; }
static int xTaskNotifyIndexed(void* task, unsigned index, uint32_t flags, int action) {
    (void)task; (void)index; (void)action; notifications |= flags; return 1;
}
static int xTaskNotifyWaitIndexed(unsigned index, uint32_t entry, uint32_t clear,
                                 uint32_t* flags, uint32_t timeout) {
    (void)index; (void)entry; (void)timeout;
    assert(++waits <= 3 && notifications);
    *flags = notifications; notifications &= ~clear; return 1;
}
static void thread_callback(void* context) {
    (void)context; ++thread_calls; notifications |= FuriEventLoopFlagStop;
}
"""
        code += function("core/event_loop.c", "furi_event_loop_restore_flags")
        code += function("core/event_loop.c", "furi_event_loop_run")
        code += r"""
int main(void) {
    FuriEventLoop loop = {(void*)1, 0, true, thread_callback, NULL};
    notifications = FuriEventLoopFlagPending | FuriEventLoopFlagThreadFlag;
    furi_event_loop_run(&loop);
    assert(pending_calls == 1 && thread_calls == 1 && waits == 3);
    assert(loop.state == FuriEventLoopStateStopped);
}
"""
        run_c(code)

    def test_stream_isr_yield(self):
        code = PREAMBLE + r"""
#define FURI_IS_IRQ_MODE() true
enum { FuriEventLoopEventIn, FuriEventLoopEventOut };
typedef struct { size_t uxDummy1[4]; } StaticStreamBuffer_t;
typedef struct { StaticStreamBuffer_t container; int event_loop_link; } FuriStreamBuffer;
typedef FuriStreamBuffer* StreamBufferHandle_t;
#define xTriggerLevelBytes uxDummy1[3]
static bool wake;
static size_t result;
static void yield_from_isr(int value) { assert(value == (wake ? pdTRUE : pdFALSE)); }
#define portYIELD_FROM_ISR(x) yield_from_isr(x)
static size_t xStreamBufferSendFromISR(StreamBufferHandle_t s, const void* d, size_t n, int* yield) {
    (void)s; (void)d; (void)n;
    // FreeRTOS only writes this output when a task is woken.
    assert(*yield == pdFALSE);
    if(wake) *yield = pdTRUE;
    return result;
}
static size_t xStreamBufferReceiveFromISR(StreamBufferHandle_t s, void* d, size_t n, int* yield) {
    return xStreamBufferSendFromISR(s, d, n, yield);
}
static size_t xStreamBufferSend(StreamBufferHandle_t s, const void* d, size_t n, uint32_t t) {
    (void)s; (void)d; (void)n; (void)t; abort();
}
static size_t xStreamBufferReceive(StreamBufferHandle_t s, void* d, size_t n, uint32_t t) {
    return xStreamBufferSend(s, d, n, t);
}
static size_t xStreamBufferBytesAvailable(StreamBufferHandle_t s) { (void)s; return result; }
static void furi_event_loop_link_notify(int* link, int event) { (void)link; (void)event; }
"""
        code += function("core/stream_buffer.c", "furi_stream_buffer_send")
        code += function("core/stream_buffer.c", "furi_stream_buffer_receive")
        code += r"""
int main(void) {
    FuriStreamBuffer stream = {0}; char byte = 0;
    for(unsigned w = 0; w < 2; ++w) {
        wake = w;
        for(result = 0; result < 2; ++result) {
            assert(furi_stream_buffer_send(&stream, &byte, 1, 0) == result);
            assert(furi_stream_buffer_receive(&stream, &byte, 1, 0) == result);
        }
    }
}
"""
        run_c(code)

    def test_queue_and_stream_allocation_bounds(self):
        code = PREAMBLE + r"""
// Exercise the firmware's 32-bit size limit even on a 64-bit host.
#undef SIZE_MAX
#define SIZE_MAX UINT32_MAX
static jmp_buf failure;
static size_t allocation_size;
#undef furi_check
#define furi_check(x) do { if(!(x)) longjmp(failure, 1); } while(0)
#define REJECT(expr) do { allocation_size = 0; \
    if(setjmp(failure) == 0) { (void)(expr); assert(!"accepted invalid input"); } \
    assert(!allocation_size); } while(0)
typedef struct { uintptr_t container; uintptr_t link[2]; uint8_t buffer[]; } FuriMessageQueue;
typedef FuriMessageQueue FuriStreamBuffer;
typedef FuriStreamBuffer* StreamBufferHandle_t;
static bool furi_kernel_is_irq_or_masked(void) { return false; }
static void* checked_malloc(size_t size) {
    allocation_size = size; assert(size < 65536); return calloc(1, size);
}
#define malloc checked_malloc
static void* xQueueCreateStatic(uint32_t count, uint32_t size, uint8_t* data, uintptr_t* container) {
    assert(allocation_size == sizeof(FuriMessageQueue) + (size_t)count * size);
    assert(data == (uint8_t*)container + sizeof(FuriMessageQueue));
    return container;
}
static StreamBufferHandle_t xStreamBufferCreateStatic(size_t size, size_t trigger,
                                                       uint8_t* data, uintptr_t* container) {
    (void)trigger;
    assert(allocation_size == sizeof(FuriStreamBuffer) + size);
    assert(data == (uint8_t*)container + sizeof(FuriStreamBuffer));
    return (StreamBufferHandle_t)container;
}
"""
        code += function("core/message_queue.c", "furi_message_queue_alloc")
        code += function("core/stream_buffer.c", "furi_stream_buffer_alloc")
        code += r"""
int main(void) {
    REJECT(furi_message_queue_alloc(0, 4));
    REJECT(furi_message_queue_alloc(4, 0));
    REJECT(furi_message_queue_alloc(0x80000000U, 2));
    REJECT(furi_message_queue_alloc(UINT32_MAX, 1));
    REJECT(furi_message_queue_alloc(1, UINT32_MAX));
    REJECT(furi_stream_buffer_alloc(0, 1));
    REJECT(furi_stream_buffer_alloc(SIZE_MAX, 1));
    REJECT(furi_stream_buffer_alloc(SIZE_MAX - sizeof(FuriStreamBuffer), 1));
    for(unsigned size = 1; size < 100; ++size) {
        free(furi_message_queue_alloc(3, size));
        free(furi_stream_buffer_alloc(size, 1));
    }
}
"""
        run_c(code)

    def test_aligned_allocations(self):
        code = PREAMBLE + r"""
static jmp_buf failure;
static size_t allocations;
#undef furi_check
#define furi_check(x) do { if(!(x)) longjmp(failure, 1); } while(0)
#define REJECT(expr) do { size_t before = allocations; \
    if(setjmp(failure) == 0) { (void)(expr); assert(!"accepted invalid input"); } \
    assert(allocations == before); } while(0)
static void* checked_malloc(size_t size) {
    ++allocations;
    assert(size < 65536);
    return calloc(1, size);
}
#define malloc checked_malloc
"""
        code += function("core/memmgr.c", "aligned_malloc")
        code += function("core/memmgr.c", "aligned_free")
        code += r"""
int main(void) {
    REJECT(aligned_malloc(8, 0));
    REJECT(aligned_malloc(8, 3));
    REJECT(aligned_malloc(SIZE_MAX, 8));
    REJECT(aligned_malloc(SIZE_MAX - 15, 32));
    REJECT(aligned_malloc(8, SIZE_MAX));
    for(size_t alignment = 1; alignment <= 4096; alignment *= 2) {
        for(size_t size = 0; size <= 257; ++size) {
            unsigned char* p = aligned_malloc(size, alignment);
            assert(p && (uintptr_t)p % alignment == 0);
            assert((uintptr_t)p % sizeof(void*) == 0);
            for(size_t i = 0; i < size; ++i) assert(p[i] == 0);
            memset(p, 0xa5, size);
            aligned_free(p);
        }
    }
    aligned_free(NULL);
}
"""
        run_c(code)

    def test_thread_labels(self):
        code = PREAMBLE + r"""
enum { FuriThreadStateStopped };
typedef struct { int state; char* name; char* appid; } FuriThread;
static unsigned allocations;
static char* copy_string(const char* text) {
    ++allocations;
    size_t size = strlen(text) + 1;
    char* copy = malloc(size);
    memcpy(copy, text, size);
    return copy;
}
#define strdup copy_string
"""
        code += function("core/thread.c", "furi_thread_set_name")
        code += function("core/thread.c", "furi_thread_set_appid")
        code += r"""
int main(void) {
    FuriThread thread = {0};
    furi_thread_set_name(&thread, "worker");
    furi_thread_set_appid(&thread, "application");
    unsigned before = allocations;
    for(unsigned i = 0; i < 100; ++i) {
        furi_thread_set_name(&thread, thread.name);
        furi_thread_set_name(&thread, "worker");
        furi_thread_set_appid(&thread, thread.appid);
        furi_thread_set_appid(&thread, "application");
    }
    assert(allocations == before);
    furi_thread_set_name(&thread, thread.name + 1);
    furi_thread_set_appid(&thread, thread.appid + 3);
    assert(strcmp(thread.name, "orker") == 0);
    assert(strcmp(thread.appid, "lication") == 0);
    furi_thread_set_name(&thread, NULL);
    furi_thread_set_appid(&thread, NULL);
    assert(!thread.name && !thread.appid);
}
"""
        run_c(code)

    def test_thread_flag_timeout(self):
        code = PREAMBLE + r"""
#define FURI_IS_IRQ_MODE() false
#define THREAD_FLAGS_INVALID_BITS 0x80000000U
#define THREAD_NOTIFY_INDEX 1
static uint32_t now, observed[8], values[8], advances[8];
static unsigned calls, count;
static uint32_t xTaskGetTickCount(void) { return now; }
static int xTaskNotifyWaitIndexed(unsigned index, uint32_t entry, uint32_t clear,
                                 uint32_t* value, uint32_t timeout) {
    assert(index == 1 && entry == 0);
    (void)clear;
    assert(calls < count);
    observed[calls] = timeout;
    *value = values[calls];
    now += advances[calls++];
    return calls < count || *value ? pdPASS : pdFAIL;
}
"""
        code += function("core/thread.c", "furi_thread_flags_wait")
        code += r"""
static void run(uint32_t start, uint32_t timeout) {
    now = start; calls = 0; count = 4;
    values[0] = values[1] = values[2] = 2; values[3] = 1;
    advances[0] = 2; advances[1] = 3; advances[2] = 1; advances[3] = 0;
    assert(furi_thread_flags_wait(1, 0, timeout) == 1);
    assert(observed[0] == timeout);
    assert(observed[1] == (timeout == FuriWaitForever ? timeout : timeout - 2));
    assert(observed[2] == (timeout == FuriWaitForever ? timeout : timeout - 5));
    assert(observed[3] == (timeout == FuriWaitForever ? timeout : timeout - 6));
}
int main(void) {
    run(0, 10); run(UINT32_MAX - 3, 10); run(0, FuriWaitForever);
    calls = 0; count = 2; values[0] = 2; values[1] = 0; advances[0] = 20;
    assert(furi_thread_flags_wait(1, 0, 10) == (uint32_t)FuriStatusErrorTimeout);
    assert(observed[1] == 0);
    calls = 0; count = 1; values[0] = 0;
    assert(furi_thread_flags_wait(1, 0, 0) == (uint32_t)FuriStatusErrorResource);
    calls = 0; count = 2; values[0] = 1; values[1] = 2;
    assert(furi_thread_flags_wait(3, FuriFlagWaitAll, 10) == 3);
}
"""
        run_c(code)

    def test_event_lifetime(self):
        code = PREAMBLE + r"""
typedef unsigned FuriEventLoopEvent;
enum { FuriEventLoopEventIn=1, FuriEventLoopEventOut=2, FuriEventLoopEventMask=3,
       FuriEventLoopEventFlagEdge=4, FuriEventLoopEventFlagOnce=8 };
typedef enum { FuriEventLoopProcessStatusComplete, FuriEventLoopProcessStatusIncomplete,
               FuriEventLoopProcessStatusFreeLater } FuriEventLoopProcessStatus;
typedef struct FuriEventLoopItem FuriEventLoopItem;
typedef struct { FuriEventLoopItem* current_item; FuriEventLoopItem* subscribed; } FuriEventLoop;
typedef struct { bool (*get_level)(void*, FuriEventLoopEvent); } Contract;
struct FuriEventLoopItem {
    FuriEventLoop* owner;
    unsigned event;
    void* object;
    const Contract* contract;
    void (*callback)(void*, void*);
    void* callback_context;
};
static void furi_event_loop_unsubscribe(FuriEventLoop* loop, void* object) {
    FuriEventLoopItem* item = loop->subscribed;
    assert(item && item->object == object);
    loop->subscribed = NULL;
    if(loop->current_item == item) item->owner = NULL;
    else free(item);
}
"""
        for name in ("furi_event_loop_process_edge_event", "furi_event_loop_process_level_event",
                     "furi_event_loop_process_event"):
            code += function("core/event_loop.c", name)
        code += r"""
static unsigned callbacks, level_calls;
static bool destroy_object;
static bool get_level(void* object, unsigned event) {
    assert(event == FuriEventLoopEventIn);
    ++level_calls;
    return *(bool*)object;
}
static void callback(void* object, void* context) {
    FuriEventLoop* loop = context;
    ++callbacks;
    if(destroy_object) {
        if(loop->subscribed) furi_event_loop_unsubscribe(loop, object);
        free(object);
    }
}
static void run(unsigned flags, bool destroy, bool level) {
    FuriEventLoop loop = {0};
    Contract contract = {get_level};
    bool* object = malloc(sizeof(bool)); *object = level;
    FuriEventLoopItem* item = malloc(sizeof(*item));
    *item = (FuriEventLoopItem){&loop, 1 | flags, object, &contract, callback, &loop};
    loop.subscribed = item; callbacks = level_calls = 0; destroy_object = destroy;
    bool invoked = level || (flags & FuriEventLoopEventFlagEdge);
    bool removed = (flags & FuriEventLoopEventFlagOnce) || (destroy && invoked);
    FuriEventLoopProcessStatus status = furi_event_loop_process_event(&loop, item);
    assert(callbacks == (unsigned)invoked && !loop.current_item);
    assert(status == (removed ? FuriEventLoopProcessStatusFreeLater :
           (invoked && !(flags & 4) ? FuriEventLoopProcessStatusIncomplete :
                                    FuriEventLoopProcessStatusComplete)));
    assert(level_calls == (flags & 4 ? 0u : (removed || !invoked ? 1u : 2u)));
    if(!(destroy && invoked)) free(object);
    free(item);
}
int main(void) {
    for(unsigned flags = 0; flags <= 12; flags += 4)
        for(unsigned destroy = 0; destroy < 2; ++destroy)
            for(unsigned level = 0; level < 2; ++level) run(flags, destroy, level);
}
"""
        run_c(code)


if __name__ == "__main__":
    unittest.main()
