#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define furi_assert assert
#define furi_check assert
typedef struct {
    char **owned_strings;
    size_t n_owned_strings;
} StrBuffer;
typedef struct {
    char text[128];
} FuriString;
typedef int Iso14443_4aData;
typedef int NfcDeviceBase;
typedef int NfcDeviceNameType;
typedef struct {
    size_t name_lengths[3];
    uint32_t pan_len, aid_len, pin;
    const char *fail_key;
    unsigned hex_reads;
    uint32_t saved_pin;
} FlipperFormat;
/* EMV_HEADER */

static unsigned live_strings;
static FuriString *furi_string_alloc(void)
{
    live_strings++;
    return calloc(1, sizeof(FuriString));
}
static void furi_string_free(FuriString *str)
{
    live_strings--;
    free(str);
}
static size_t furi_string_size(const FuriString *str)
{
    return strlen(str->text);
}
static const char *furi_string_get_cstr(const FuriString *str)
{
    return str->text;
}
static char *clone_string(const char *str)
{
    char *copy = malloc(strlen(str) + 1);
    memcpy(copy, str, strlen(str) + 1);
    return copy;
}
#define strdup clone_string
static bool accepts(FlipperFormat *ff, const char *key)
{
    return !ff->fail_key || strcmp(ff->fail_key, key) != 0;
}
static bool iso14443_4a_load(Iso14443_4aData *data, FlipperFormat *ff, uint32_t version)
{
    (void)data;
    (void)version;
    return accepts(ff, "base");
}
static bool iso14443_4a_save(const Iso14443_4aData *data, FlipperFormat *ff)
{
    (void)data;
    return accepts(ff, "base");
}
static bool flipper_format_read_string(FlipperFormat *ff, const char *key, FuriString *str)
{
    if (!accepts(ff, key))
        return false;
    size_t index = strcmp(key, "Cardholder name") == 0    ? 0
                   : strcmp(key, "Application name") == 0 ? 1
                                                          : 2;
    memset(str->text, 'A', ff->name_lengths[index]);
    str->text[ff->name_lengths[index]] = '\0';
    return true;
}
static bool flipper_format_read_uint32(FlipperFormat *ff, const char *key, uint32_t *out,
                                       size_t count)
{
    assert(count == 1);
    *out = strcmp(key, "PAN length") == 0   ? ff->pan_len
           : strcmp(key, "AID length") == 0 ? ff->aid_len
                                            : ff->pin;
    return accepts(ff, key);
}
static bool flipper_format_read_hex(FlipperFormat *ff, const char *key, uint8_t *out, size_t count)
{
    ff->hex_reads++;
    if (!accepts(ff, key))
        return false;
    memset(out, 0x42, count);
    return true;
}
static bool flipper_format_write_comment_cstr(FlipperFormat *ff, const char *str)
{
    (void)str;
    return accepts(ff, "comment");
}
static bool flipper_format_write_string_cstr(FlipperFormat *ff, const char *key, const char *str)
{
    assert(strlen(str) <= 24);
    return accepts(ff, key);
}
static bool flipper_format_write_hex(FlipperFormat *ff, const char *key, const uint8_t *data,
                                     size_t count)
{
    (void)data;
    (void)count;
    return accepts(ff, key);
}
static bool flipper_format_write_uint32(FlipperFormat *ff, const char *key, const uint32_t *data,
                                        size_t count)
{
    assert(count == 1);
    if (strcmp(key, "PIN try counter") == 0)
        ff->saved_pin = *data;
    return accepts(ff, key);
}
/* PRODUCTION_CODE */

int main(void)
{
    StrBuffer buffer = {0};
    str_buffer_clear_all_clones(&buffer);
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        const char *first = str_buffer_make_owned_clone(&buffer, "first");
        for (unsigned i = 0; i < 20; i++)
            str_buffer_make_owned_clone(&buffer, "next");
        assert(strcmp(first, "first") == 0);
        str_buffer_clear_all_clones(&buffer);
        assert(buffer.n_owned_strings == 0 && buffer.owned_strings == NULL);
        str_buffer_clear_all_clones(&buffer);
    }
    EmvData data = {0};
    FlipperFormat valid = {.name_lengths = {24, 16, 16}, .pan_len = 10, .aid_len = 16, .pin = 255};
    FlipperFormat ff = valid;
    assert(emv_load(&data, &ff, 1));
    assert(strlen(data.emv_application.cardholder_name) == 24);
    assert(strlen(data.emv_application.application_name) == 16);
    assert(strlen(data.emv_application.application_label) == 16);
    data.emv_application.transaction_counter = 0xffff;
    assert(emv_save(&data, &ff));
    assert(ff.saved_pin == 255);
    for (size_t i = 0; i < 3; i++) {
        ff = valid;
        ff.name_lengths[i]++;
        assert(!emv_load(&data, &ff, 1));
        assert(ff.hex_reads == 0 && live_strings == 0);
    }
    const uint32_t bad_lengths[] = {17, 256, UINT32_MAX};
    for (size_t i = 0; i < 3; i++) {
        ff = valid;
        ff.pan_len = bad_lengths[i];
        assert(!emv_load(&data, &ff, 1) && ff.hex_reads == 0);
        ff = valid;
        ff.aid_len = bad_lengths[i];
        assert(!emv_load(&data, &ff, 1) && ff.hex_reads == 1);
    }
    ff = valid;
    ff.pan_len = 11;
    assert(!emv_load(&data, &ff, 1));
    ff = valid;
    ff.pin = 256;
    assert(!emv_load(&data, &ff, 1));
    const char *failures[] = {"base",
                              "Cardholder name",
                              "Application name",
                              "Application label",
                              "PAN length",
                              "PAN",
                              "AID length",
                              "AID",
                              "PIN try counter"};
    for (size_t i = 0; i < sizeof(failures) / sizeof(*failures); i++) {
        ff = valid;
        ff.fail_key = failures[i];
        assert(!emv_load(&data, &ff, 1));
        assert(live_strings == 0);
    }
    ff = valid;
    assert(emv_load(&data, &ff, 1));
    data.emv_application.pan_len = 11;
    assert(!emv_save(&data, &ff));
    data.emv_application.pan_len = 10;
    data.emv_application.aid_len = 17;
    assert(!emv_save(&data, &ff));
    data.emv_application.aid_len = 16;
    memset(data.emv_application.application_label, 'X',
           sizeof(data.emv_application.application_label));
    assert(!emv_save(&data, &ff));
    assert(live_strings == 0);
    return 0;
}
