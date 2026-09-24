
static void test_value(uint32_t value) {
    uint8_t data[10];
    uint32_t decoded = 0;
    size_t size = varint_uint32_pack(value, data);
    assert(size == varint_uint32_length(value));
    assert(varint_uint32_unpack(&decoded, data, size) == size && decoded == value);
    for(size_t truncated = 0; truncated < size; ++truncated) {
        decoded = 123;
        assert(varint_uint32_unpack(&decoded, data, truncated) == 0 && decoded == 123);
    }
    int32_t signed_value = (int32_t)(value >> 1);
    if(value & 1) signed_value = -signed_value - 1;
    size = varint_int32_pack(signed_value, data);
    int32_t signed_decoded = 0;
    assert(size == varint_int32_length(signed_value));
    assert(varint_uint32_unpack(&decoded, data, size) == size && decoded == value);
    assert(
        varint_int32_unpack(&signed_decoded, data, size) == size &&
        signed_decoded == signed_value);
    size_t second = varint_uint32_pack(~value, data + size);
    uint32_t a = 0, b = 0;
    size_t consumed = 0;
    assert(varint_pair_unpack(data, size + second, &a, &b, &consumed));
    assert(a == value && b == ~value && consumed == size + second);
    for(size_t truncated = 0; truncated < consumed; ++truncated) {
        a = b = 123;
        size_t sentinel = 456;
        assert(!varint_pair_unpack(data, truncated, &a, &b, &sentinel));
        assert(a == 123 && b == 123 && sentinel == 456);
    }
}

int main(void) {
    const uint32_t edges[] = {
        0,
        1,
        127,
        128,
        16383,
        16384,
        0x1fffff,
        0x200000,
        0xfffffff,
        0x10000000,
        INT32_MAX,
        0x80000000,
        UINT32_MAX};
    for(size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i)
        test_value(edges[i]);
    uint32_t state = 1;
    for(size_t i = 0; i < 10000; ++i) {
        state = state * 1664525U + 1013904223U;
        test_value(state);
    }
    uint8_t invalid[][6] = {{0x80, 0x80, 0x80, 0x80, 0x10, 0}, {0xff, 0xff, 0xff, 0xff, 0xff, 0}};
    for(size_t i = 0; i < 2; ++i) {
        uint32_t value = 123;
        int32_t signed_value = -123;
        assert(varint_uint32_unpack(&value, invalid[i], 6) == 0 && value == 123);
        assert(varint_int32_unpack(&signed_value, invalid[i], 6) == 0 && signed_value == -123);
    }
    puts("Varints: signed extremes, 10000 random roundtrips, truncation and overflow passed");
    return 0;
}
