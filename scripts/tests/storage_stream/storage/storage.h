#pragma once

/* Host-only storage/string adapter for the production configuration loader. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define furi_check(condition, ...) assert(condition)
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
#define APP_DATA_PATH(path) path

typedef struct {
    char *data;
    size_t size;
} FuriString;

typedef struct {
    const char *data;
    size_t size;
    size_t position;
    size_t max_read;
    size_t fail_after;
    bool error;
} File;

typedef struct {
    File file;
} Storage;

enum { FSAM_READ, FSAM_WRITE, FSOM_OPEN_EXISTING, FSOM_CREATE_ALWAYS, FSE_OK };

FuriString *furi_string_alloc(void);
void furi_string_free(FuriString *string);
const char *furi_string_get_cstr(const FuriString *string);
size_t furi_string_size(const FuriString *string);
bool furi_string_empty(const FuriString *string);
void furi_string_set_strn(FuriString *string, const char *data, size_t size);
void furi_string_set_str(FuriString *string, const char *data);
void furi_string_push_back(FuriString *string, char value);
void furi_string_cat_printf(FuriString *string, const char *format, ...);
File *storage_file_alloc(Storage *storage);
void storage_file_free(File *file);
bool storage_file_open(File *file, const char *path, int access, int mode);
void storage_file_close(File *file);
uint64_t storage_file_size(File *file);
size_t storage_file_read(File *file, void *data, size_t size);
size_t storage_file_write(File *file, const void *data, size_t size);
int storage_file_get_error(File *file);
