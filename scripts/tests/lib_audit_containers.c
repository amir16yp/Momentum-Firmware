
static size_t allocations, releases;
static void *tracked_malloc(size_t size)
{
    ++allocations;
    void *result = calloc(1, size); // Firmware malloc zeroes allocations.
    assert(result);
    return result;
}
static void tracked_free(void *ptr)
{
    if (ptr)
        ++releases;
    free(ptr);
}
#define malloc tracked_malloc
#define free tracked_free
/* ARRAY */

typedef struct {
    void *queue[256];
    size_t head, count, capacity;
} FuriStreamBuffer;
static FuriStreamBuffer *furi_stream_buffer_alloc(size_t size, size_t trigger)
{
    assert(trigger == sizeof(void *));
    FuriStreamBuffer *stream = malloc(sizeof(FuriStreamBuffer));
    stream->capacity = size / sizeof(void *);
    assert(stream->capacity <= 256);
    return stream;
}
static void furi_stream_buffer_free(FuriStreamBuffer *stream)
{
    free(stream);
}
static void furi_stream_buffer_reset(FuriStreamBuffer *stream)
{
    stream->head = stream->count = 0;
}
static size_t furi_stream_buffer_send(FuriStreamBuffer *stream, const void *ptr, size_t size,
                                      uint32_t timeout)
{
    (void)timeout;
    assert(size == sizeof(void *));
    if (stream->count == stream->capacity)
        return 0;
    memcpy(&stream->queue[(stream->head + stream->count++) % stream->capacity], ptr, size);
    return size;
}
static size_t furi_stream_buffer_receive(FuriStreamBuffer *stream, void *ptr, size_t size,
                                         uint32_t timeout)
{
    (void)timeout;
    assert(size == sizeof(void *));
    if (!stream->count)
        return 0;
    memcpy(ptr, &stream->queue[stream->head], size);
    stream->head = (stream->head + 1) % stream->capacity;
    --stream->count;
    return size;
}
#define FURI_CRITICAL_ENTER()
#define FURI_CRITICAL_EXIT()
/* POOL */
#undef malloc
#undef free

static size_t initialized, reset, copied;
static void element_init(void *p)
{
    ++initialized;
    *(uint32_t *)p = 123;
}
static void element_reset(void *p)
{
    ++reset;
    *(uint32_t *)p = 0;
}
static void element_copy(void *p, const void *q)
{
    ++copied;
    *(uint32_t *)p = *(const uint32_t *)q;
}

int main(void)
{
    const SimpleArrayConfig config = {.type_size = sizeof(uint32_t)};
    SimpleArray *a = simple_array_alloc(&config);
    SimpleArray *b = simple_array_alloc(&config);
    assert(simple_array_is_equal(a, b));
    simple_array_init(a, 4);
    uint32_t *data = simple_array_get_data(a);
    for (size_t i = 0; i < 4; ++i)
        data[i] = i + 1;
    size_t before = allocations;
    simple_array_copy(a, a);
    assert(simple_array_get_count(a) == 4 && allocations == before && data[3] == 4);
    simple_array_copy(b, a);
    assert(simple_array_is_equal(a, b));
    *(uint32_t *)simple_array_get(b, 3) = 999;
    assert(!simple_array_is_equal(a, b));
    simple_array_reset(a);
    simple_array_copy(b, a);
    assert(simple_array_get_count(b) == 0 && simple_array_is_equal(a, b));
    simple_array_free(a);
    simple_array_free(b);

    const SimpleArrayConfig callbacks = {element_init, element_reset, element_copy,
                                         sizeof(uint32_t)};
    a = simple_array_alloc(&callbacks);
    b = simple_array_alloc(&callbacks);
    simple_array_init(a, 3);
    simple_array_init(b, 2);
    simple_array_copy(b, a);
    assert(initialized == 8 && reset == 2 && copied == 3);
    simple_array_copy(b, b);
    assert(initialized == 8 && reset == 2 && copied == 3);
    simple_array_free(a);
    simple_array_free(b);
    assert(reset == initialized);

    const size_t sizes[] = {1, 7, 16, 511};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        before = allocations;
        BufferStream *pool = buffer_stream_alloc(sizes[s], 130);
        assert(allocations == before + 2); // One pool allocation, one queue allocation.
        for (size_t i = 0; i < 130; ++i) {
            assert((uintptr_t)pool->buffers[i].data % _Alignof(max_align_t) == 0);
            assert(pool->buffers[i].max_data_size == sizes[s]);
            memset(pool->buffers[i].data, i, sizes[s]);
        }
        uint8_t bytes[511];
        memset(bytes, 0xa5, sizeof(bytes));
        assert(!buffer_write(&pool->buffers[0], bytes, SIZE_MAX));
        assert(!buffer_stream_send_from_isr(pool, bytes, sizes[s] + 1));
        assert(pool->stream->count == 0);
        for (size_t i = 0; i < 130; ++i)
            assert(buffer_stream_send_from_isr(pool, bytes, sizes[s]));
        assert(pool->index == 129); // Indices above INT8_MAX remain usable.
        assert(!buffer_stream_send_from_isr(pool, bytes, sizes[s]));
        assert(!buffer_stream_send_from_isr(pool, bytes, sizes[s]));
        assert(buffer_stream_get_overrun_count(pool) == 2);
        for (size_t i = 0; i < 130; ++i) {
            Buffer *buffer = buffer_stream_receive(pool, 0);
            assert(buffer == &pool->buffers[i]);
            assert(buffer_get_size(buffer) == sizes[s]);
            assert(!memcmp(buffer_get_data(buffer), bytes, sizes[s]));
            buffer_reset(buffer);
            // Recover while the last queued buffer remains occupied. It must not be queued twice.
            if (i == 0)
                assert(buffer_stream_send_from_isr(pool, bytes, sizes[s]));
        }
        assert(buffer_stream_receive(pool, 0) == NULL);
        buffer_stream_reset(pool);
        for (size_t i = 0; i < 130; ++i)
            assert(buffer_get_size(&pool->buffers[i]) == 0);
        buffer_stream_free(pool);
    }
    assert(allocations == releases);
    puts("Arrays: self-copy, full equality, callbacks; pools: aligned storage, 130 buffers, "
         "balanced ownership passed");
    return 0;
}
