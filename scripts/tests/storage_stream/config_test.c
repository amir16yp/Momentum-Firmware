#include <stdarg.h>
#include "app_config.h"

static size_t live_strings;

FuriString *furi_string_alloc(void)
{
    FuriString *string = calloc(1, sizeof(*string));
    string->data = calloc(1, 1);
    live_strings++;
    return string;
}

void furi_string_free(FuriString *string)
{
    free(string->data);
    free(string);
    live_strings--;
}

const char *furi_string_get_cstr(const FuriString *string)
{
    return string->data;
}
size_t furi_string_size(const FuriString *string)
{
    return string->size;
}
bool furi_string_empty(const FuriString *string)
{
    return string->size == 0;
}

void furi_string_set_strn(FuriString *string, const char *data, size_t size)
{
    string->data = realloc(string->data, size + 1);
    memcpy(string->data, data, size);
    string->data[size] = 0;
    string->size = size;
}

void furi_string_set_str(FuriString *string, const char *data)
{
    furi_string_set_strn(string, data, strlen(data));
}

void furi_string_push_back(FuriString *string, char value)
{
    string->data = realloc(string->data, string->size + 2);
    string->data[string->size++] = value;
    string->data[string->size] = 0;
}

void furi_string_cat_printf(FuriString *string, const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    assert(length >= 0 && length < sizeof(buffer));
    for (int i = 0; i < length; i++)
        furi_string_push_back(string, buffer[i]);
}

File *storage_file_alloc(Storage *storage)
{
    return &storage->file;
}
void storage_file_free(File *file)
{
    (void)file;
}
bool storage_file_open(File *file, const char *path, int access, int mode)
{
    (void)path;
    (void)access;
    (void)mode;
    file->position = 0;
    return true;
}
void storage_file_close(File *file)
{
    (void)file;
}
uint64_t storage_file_size(File *file)
{
    return file->size;
}
int storage_file_get_error(File *file)
{
    return file->error ? -1 : FSE_OK;
}

size_t storage_file_read(File *file, void *data, size_t size)
{
    assert(size <= 512);
    if (file->position >= file->fail_after)
        return 0;
    size = MIN(size, file->max_read);
    size = MIN(size, file->size - file->position);
    size = MIN(size, file->fail_after - file->position);
    memcpy(data, file->data + file->position, size);
    file->position += size;
    return size;
}

size_t storage_file_write(File *file, const void *data, size_t size)
{
    (void)file;
    (void)data;
    return size;
}

static void check_load(const char *text, size_t max_read, size_t fail_after, bool error)
{
    Storage storage = {.file = {
                           .data = text,
                           .size = strlen(text),
                           .max_read = max_read,
                           .fail_after = fail_after,
                           .error = error,
                       }};
    AppConfig config = {0};
    app_config_init(&config);
    AppConfig before = config;
    app_config_load(&config, &storage);
    if (fail_after < strlen(text) || error) {
        assert(memcmp(&config, &before, sizeof(config)) == 0);
    } else {
        assert(config.sensor_type == SensorType_INA228);
        assert(config.i2c_address == 0x45);
        assert(config.shunt_resistor == 125.5);
        assert(!config.led_blinking);
        assert(config.current_precision == SensorPrecision_High);
    }
    assert(live_strings == 0);
}

int main(void)
{
    const char *records = "[app]\r\nledBlinking=0\r\n[sensor]\r\nsensorType=INA228\r\n"
                          "i2cAddress=0x45\r\nshuntResistor=125.5\r\ncurrentPrecision=High";
    char text[4096];
    // Move every token and CRLF pair across both sides of a 512-byte boundary.
    for (size_t padding = 0; padding < 1024; padding++) {
        memset(text, ' ', padding);
        memcpy(text + padding, records, strlen(records) + 1);
        check_load(text, 512, SIZE_MAX, false);
    }
    // A record longer than several chunks, followed by a section and final unterminated line.
    text[0] = '#';
    memset(text + 1, 'x', 1700);
    text[1701] = '\n';
    memcpy(text + 1702, records, strlen(records) + 1);
    check_load(text, 512, SIZE_MAX, false);
    const char *long_value = "[sensor]\nshuntResistor=";
    size_t prefix = strlen(long_value);
    memcpy(text, long_value, prefix);
    memset(text + prefix, ' ', 1700);
    snprintf(text + prefix + 1700, sizeof(text) - prefix - 1700, "125.5\n%s\n", records);
    check_load(text, 512, SIZE_MAX, false);
    check_load("[app]\nledBlinking=0\n[sensor]\nsensorType=INA228\ni2cAddress=0x45\n"
               "shuntResistor=125.5\ncurrentPrecision=High\n[unknown]\ni2cAddress=0\n",
               512, SIZE_MAX, false);
    for (size_t chunk = 1; chunk <= 512; chunk++)
        check_load(records, chunk, SIZE_MAX, false);
    // No partially loaded settings may escape when reading fails or stops early.
    for (size_t stop = 0; stop < strlen(records); stop++)
        check_load(records, 17, stop, false);
    check_load(records, 512, SIZE_MAX, true);
    Storage empty = {.file = {.data = "", .max_read = 512, .fail_after = SIZE_MAX}};
    AppConfig config = {0};
    app_config_init(&config);
    AppConfig before = config;
    app_config_load(&config, &empty);
    assert(memcmp(&config, &before, sizeof(config)) == 0 && live_strings == 0);
    puts("Storage streaming boundary and read-failure tests passed");
    return 0;
}
