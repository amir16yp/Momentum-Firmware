
static uint64_t reference_get(const uint8_t* data, size_t offset, size_t length) {
    uint64_t result = 0;
    for(size_t i = 0; i < length; ++i)
        result = (result << 1) | ((data[(offset + i) / 8] >> (7 - (offset + i) % 8)) & 1);
    return result;
}

static uint16_t reference_reverse(uint16_t value, size_t bits) {
    uint16_t result = 0;
    for(size_t i = 0; i < bits; ++i) {
        result = (result << 1) | (value & 1);
        value >>= 1;
    }
    return result;
}

static uint16_t reference_crc(
    const uint8_t* data,
    size_t size,
    unsigned bits,
    uint16_t poly,
    uint16_t initial,
    bool reflect_in,
    bool reflect_out,
    uint16_t xor_out) {
    uint16_t crc = initial;
    const uint16_t mask = bits == 8 ? 0xff : 0xffff;
    for(size_t i = 0; i < size; ++i) {
        uint8_t byte = reflect_in ? reference_reverse(data[i], 8) : data[i];
        for(unsigned bit = 0; bit < 8; ++bit) {
            bool feedback = ((crc >> (bits - 1)) ^ (byte >> (7 - bit))) & 1;
            crc = ((crc << 1) ^ (feedback ? poly : 0)) & mask;
        }
    }
    return (reflect_out ? reference_reverse(crc, bits) : crc) ^ xor_out;
}

int main(void) {
    for(unsigned value = 0; value <= UINT16_MAX; ++value)
        assert(bit_lib_reverse_16_fast(value) == reference_reverse(value, 16));
    for(unsigned value = 0; value < 256; ++value) {
        assert(bit_lib_reverse_8_fast(value) == reference_reverse(value, 8));
        for(size_t offset = 0; offset < 16; ++offset) {
            for(unsigned length = 1; length <= 8; ++length) {
                uint8_t data[] = {0xa5, 0x5a, 0xa5};
                uint8_t expected[sizeof(data)];
                memcpy(expected, data, sizeof(data));
                for(unsigned bit = 0; bit < length; ++bit) {
                    size_t pos = offset + bit;
                    uint8_t mask = 1U << (7 - pos % 8);
                    expected[pos / 8] = (expected[pos / 8] & ~mask) |
                                        (((value >> (length - 1 - bit)) & 1) ? mask : 0);
                }
                bit_lib_set_bits(data, offset, value, length);
                assert(memcmp(data, expected, sizeof(data)) == 0);
                assert(bit_lib_get_bits(data, offset, length) == (value & ((1U << length) - 1)));
            }
        }
        // Exact-sized allocation: no read of a second byte for an in-byte field.
        uint8_t* last = malloc(1);
        *last = value;
        for(unsigned offset = 0; offset < 8; ++offset)
            for(unsigned length = 0; length <= 8 - offset; ++length)
                assert(
                    bit_lib_get_bits(last, offset, length) == reference_get(last, offset, length));
        free(last);
    }
    bit_lib_push_bit(NULL, 0, false);
    bit_lib_reverse_bits(NULL, 0, 0);
    uint8_t data[257];
    for(size_t i = 0; i < sizeof(data); ++i)
        data[i] = i * 73 + 19;
    for(size_t offset = 0; offset < 16; ++offset) {
        for(unsigned length = 0; length <= 64; ++length) {
            uint64_t expected = reference_get(data, offset, length);
            assert(bit_lib_get_bits_64(data, offset, length) == expected);
            if(length <= 32) assert(bit_lib_get_bits_32(data, offset, length) == expected);
            if(length <= 16) assert(bit_lib_get_bits_16(data, offset, length) == expected);
        }
    }
    for(size_t size = 0; size <= sizeof(data); ++size) {
        for(unsigned flags = 0; flags < 4; ++flags) {
            bool in = flags & 1, out = flags & 2;
            assert(
                bit_lib_crc8(data, size, 0x31, 0xa5, in, out, 0x5a) ==
                reference_crc(data, size, 8, 0x31, 0xa5, in, out, 0x5a));
            assert(
                bit_lib_crc16(data, size, 0x1021, 0xa5a5, in, out, 0x5a5a) ==
                reference_crc(data, size, 16, 0x1021, 0xa5a5, in, out, 0x5a5a));
        }
        for(unsigned type = 0; type < 2; ++type) {
            uint16_t crc = type ? 0xe012 : 0xffff;
            for(size_t i = 0; i < size; ++i) {
                crc ^= data[i];
                for(unsigned bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
            }
            assert(iso13239_crc_calculate(type, data, size) == (uint16_t)(type ? crc : ~crc));
        }
    }
    puts("Bit fields, exact boundaries, 65536 reversals and independent CRC references passed");
    return 0;
}
