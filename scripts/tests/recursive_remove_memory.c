#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAME_LENGTH     254
#define FURI_STRING_FAILURE SIZE_MAX
#define furi_check          assert
#define furi_assert         assert

typedef struct {
    char* text;
} FuriString;
typedef struct {
    bool directory;
} FileInfo;
typedef struct {
    char path[1024];
    bool directory;
    bool removed;
} Node;
typedef struct {
    Node nodes[64];
    size_t count;
    const char* fail_open;
} Storage;
typedef struct {
    Storage* storage;
    char path[1024];
    size_t next;
} File;
typedef int FS_Error;
enum {
    FSE_OK,
    FSE_ERROR
};
static size_t live_strings, string_allocations, live_files;

static FuriString* furi_string_alloc_set(const char* text) {
    FuriString* value = malloc(sizeof(*value));
    value->text = malloc(strlen(text) + 1);
    strcpy(value->text, text);
    ++live_strings;
    ++string_allocations;
    assert(live_strings == 1);
    return value;
}
static void furi_string_free(FuriString* value) {
    free(value->text);
    free(value);
    --live_strings;
}
static const char* furi_string_get_cstr(FuriString* value) {
    return value->text;
}
static size_t furi_string_size(FuriString* value) {
    return strlen(value->text);
}
static void furi_string_cat_printf(FuriString* value, const char* format, const char* name) {
    assert(strcmp(format, "/%s") == 0);
    size_t length = strlen(value->text);
    value->text = realloc(value->text, length + strlen(name) + 2);
    sprintf(value->text + length, "/%s", name);
}
static void furi_string_left(FuriString* value, size_t length) {
    assert(length <= strlen(value->text));
    value->text[length] = '\0';
}
static int furi_string_cmp(FuriString* value, const char* other) {
    return strcmp(value->text, other);
}
static size_t furi_string_search_rchar(FuriString* value, char character) {
    const char* found = strrchr(value->text, character);
    return found ? (size_t)(found - value->text) : SIZE_MAX;
}
static FS_Error storage_common_remove(Storage* storage, const char* path) {
    for(size_t i = 0; i < storage->count; ++i) {
        Node* node = &storage->nodes[i];
        if(node->removed || strcmp(node->path, path)) continue;
        size_t length = strlen(path);
        for(size_t j = 0; j < storage->count; ++j) {
            Node* child = &storage->nodes[j];
            if(!child->removed && !strncmp(child->path, path, length) &&
               child->path[length] == '/')
                return FSE_ERROR;
        }
        node->removed = true;
        return FSE_OK;
    }
    return FSE_OK; // Missing paths are already removed.
}
static bool storage_simply_remove(Storage* storage, const char* path) {
    return storage_common_remove(storage, path) == FSE_OK;
}
static File* storage_file_alloc(Storage* storage) {
    File* file = calloc(1, sizeof(*file));
    file->storage = storage;
    ++live_files;
    return file;
}
static void storage_file_free(File* file) {
    free(file);
    --live_files;
}
static bool storage_dir_open(File* file, const char* path) {
    if(file->storage->fail_open && !strcmp(path, file->storage->fail_open)) return false;
    strcpy(file->path, path);
    file->next = 0;
    return true;
}
static void storage_dir_close(File* file) {
    (void)file;
}
static bool storage_dir_read(File* file, FileInfo* info, char* name, size_t capacity) {
    size_t length = strlen(file->path);
    while(file->next < file->storage->count) {
        Node* node = &file->storage->nodes[file->next++];
        if(node->removed || strncmp(node->path, file->path, length) || node->path[length] != '/')
            continue;
        const char* child = node->path + length + 1;
        if(strchr(child, '/')) continue;
        assert(strlen(child) < capacity);
        strcpy(name, child);
        info->directory = node->directory;
        return true;
    }
    return false;
}
static bool file_info_is_dir(FileInfo* info) {
    return info->directory;
}

/* PRODUCTION_FUNCTION */

static void add(Storage* storage, const char* path, bool directory) {
    Node* node = &storage->nodes[storage->count++];
    strcpy(node->path, path);
    node->directory = directory;
}
int main(void) {
    for(unsigned iteration = 0; iteration < 100; ++iteration) {
        Storage storage = {0};
        add(&storage, "/ext/tree", true);
        add(&storage, "/ext/tree/first", false);
        add(&storage, "/ext/tree/nested", true);
        add(&storage, "/ext/tree/nested/child", false);
        add(&storage, "/ext/tree/nested/deeper", true);
        add(&storage, "/ext/tree/nested/deeper/last", false);
        add(&storage, "/ext/tree/sibling", true);
        add(&storage, "/ext/tree/sibling/file", false);
        add(&storage, "/ext/tree/empty", true);
        char long_path[300] = "/ext/tree/";
        memset(long_path + 10, 'x', MAX_NAME_LENGTH - 1);
        long_path[10 + MAX_NAME_LENGTH - 1] = '\0';
        add(&storage, long_path, false);
        add(&storage, "/ext/tree-other", false);
        string_allocations = 0;
        assert(storage_simply_remove_recursive(&storage, "/ext/tree"));
        assert(string_allocations == 1);
        assert(live_strings == 0 && live_files == 0);
        for(size_t i = 0; i + 1 < storage.count; ++i)
            assert(storage.nodes[i].removed);
        assert(!storage.nodes[storage.count - 1].removed);
        assert(storage_simply_remove_recursive(&storage, "/ext/missing"));
        assert(storage_simply_remove_recursive(&storage, "/ext/tree-other"));
        assert(string_allocations == 1);
    }
    const char* failures[] = {"/ext/tree", "/ext/tree/child"};
    for(size_t i = 0; i < 2; ++i) {
        Storage storage = {0};
        add(&storage, "/ext/tree", true);
        add(&storage, "/ext/tree/child", true);
        add(&storage, "/ext/tree/child/file", false);
        storage.fail_open = failures[i];
        assert(!storage_simply_remove_recursive(&storage, "/ext/tree"));
        assert(live_strings == 0 && live_files == 0);
    }
    return 0;
}
