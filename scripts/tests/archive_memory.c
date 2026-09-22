#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <m-array.h>
#include <m-algo.h>

typedef struct {
    char text[128];
} FuriString;
static size_t live_strings;
static size_t live_icons;
static size_t icon_allocations;
static struct {
    bool sort_dirs_first;
} momentum_settings;
static FuriString* furi_string_alloc(void) {
    FuriString* string = calloc(1, sizeof(FuriString));
    assert(string);
    live_strings++;
    return string;
}
static FuriString* furi_string_alloc_set(const FuriString* source) {
    assert(source);
    FuriString* string = furi_string_alloc();
    *string = *source;
    return string;
}
static void furi_string_set(FuriString* dest, const FuriString* source) {
    assert(dest && source);
    *dest = *source;
}
static void furi_string_free(FuriString* string) {
    assert(string && live_strings);
    live_strings--;
    free(string);
}
static bool furi_string_empty(const FuriString* string) {
    assert(string);
    return !string->text[0];
}
static int furi_string_cmpi(const FuriString* left, const FuriString* right) {
    assert(left && right);
    size_t i = 0;
    while(left->text[i] &&
          tolower((unsigned char)left->text[i]) == tolower((unsigned char)right->text[i]))
        i++;
    return tolower((unsigned char)left->text[i]) - tolower((unsigned char)right->text[i]);
}
static void* icon_alloc(size_t size) {
    assert(size == 32);
    live_icons++;
    icon_allocations++;
    void* data = malloc(size);
    assert(data);
    return data;
}
static void icon_free(void* data) {
    if(!data) return;
    assert(live_icons);
    live_icons--;
    free(data);
}
#define malloc icon_alloc
#define free   icon_free
/* HELPERS */
#undef malloc
#undef free
/* ARRAYS */

static void make_app(ArchiveFile_t* entry, const char* name, uint8_t pattern) {
    entry->type = ArchiveFileTypeApplication;
    entry->custom_name = furi_string_alloc();
    strcpy(entry->custom_name->text, name);
    entry->custom_icon_data = icon_alloc(32);
    memset(entry->custom_icon_data, pattern, 32);
}
static void test_assignment(void) {
    ArchiveFile_t source, dest, plain;
    ArchiveFile_t_init(&source);
    ArchiveFile_t_init(&dest);
    ArchiveFile_t_init(&plain);
    assert(live_strings == 3);
    strcpy(source.path->text, "/ext/apps/a.fap");
    strcpy(plain.path->text, "/ext/readme.txt");
    make_app(&source, "App", 0x3c);
    make_app(&dest, "Old", 0x5a);
    uint8_t* original = dest.custom_icon_data;
    size_t before = icon_allocations;
    for(size_t i = 0; i < 1000; i++) {
        ArchiveFile_t_set(&dest, &source);
        assert(dest.custom_icon_data == original);
        assert(dest.custom_icon_data != source.custom_icon_data);
        assert(memcmp(dest.custom_icon_data, source.custom_icon_data, 32) == 0);
        ArchiveFile_t_set(&dest, &dest);
        assert(icon_allocations == before && live_icons == 2);
    }
    ArchiveFile_t_set(&dest, &plain);
    assert(dest.custom_icon_data == NULL && live_icons == 1);
    assert(dest.custom_name == NULL && live_strings == 4);
    ArchiveFile_t_set(&dest, &source);
    assert(live_icons == 2 && icon_allocations == before + 1);
    assert(dest.custom_name != source.custom_name && live_strings == 5);
    source.custom_name->text[0] = 'X';
    assert(strcmp(dest.custom_name->text, "App") == 0);
    source.custom_icon_data[0] ^= 0xff;
    assert(dest.custom_icon_data[0] == 0x3c);
    ArchiveFile_t_clear(&source);
    ArchiveFile_t_clear(&dest);
    ArchiveFile_t_clear(&plain);
    assert(live_icons == 0 && live_strings == 0);
}
static void test_empty_name(void) {
    ArchiveFile_t app, plain, copy;
    ArchiveFile_t_init(&app);
    ArchiveFile_t_init(&plain);
    strcpy(app.path->text, "Alpha");
    strcpy(plain.path->text, "Beta");
    make_app(&app, "", 0);
    /* Failed metadata loads may leave an application name without an icon. */
    icon_free(app.custom_icon_data);
    app.custom_icon_data = NULL;
    momentum_settings.sort_dirs_first = false;
    assert(ArchiveFile_t_cmp(&app, &plain) < 0);
    assert(ArchiveFile_t_cmp(&plain, &app) > 0);
    ArchiveFile_t_init_set(&copy, &app);
    assert(copy.custom_name && copy.custom_name != app.custom_name);
    assert(ArchiveFile_t_cmp(&copy, &app) == 0);
    ArchiveFile_t_clear(&copy);
    ArchiveFile_t_clear(&app);
    ArchiveFile_t_clear(&plain);
    assert(live_icons == 0 && live_strings == 0);
}
static void test_array(void) {
    files_array_t entries;
    files_array_init(entries);
    for(size_t i = 0; i < 100; i++) {
        ArchiveFile_t entry;
        ArchiveFile_t_init(&entry);
        snprintf(entry.path->text, sizeof(entry.path->text), "/ext/file%03zu", 100 - i);
        if(i % 3 == 0)
            make_app(&entry, i % 2 ? "Alpha" : "Zulu", (uint8_t)i);
        else if(i % 3 == 1)
            entry.type = ArchiveFileTypeFolder;
        files_array_push_back(entries, entry);
        ArchiveFile_t_clear(&entry);
    }
    assert(live_strings == 134 && live_icons == 34);
    for(unsigned folders = 0; folders < 2; folders++) {
        momentum_settings.sort_dirs_first = folders;
        files_array_sort(entries);
        for(size_t i = 1; i < files_array_size(entries); i++) {
            assert(
                ArchiveFile_t_cmp(files_array_get(entries, i - 1), files_array_get(entries, i)) <=
                0);
        }
    }
    files_array_set_at(entries, 5, *files_array_get(entries, 6));
    files_array_pop_at(NULL, entries, 10);
    files_array_clear(entries);
    assert(live_icons == 0 && live_strings == 0);
}
int main(void) {
    for(size_t i = 0; i < 100; i++) {
        test_assignment();
        test_empty_name();
        test_array();
    }
    puts("Archive: assignment, self-copy, deep-copy, sorting/removal and cleanup passed");
    return 0;
}
