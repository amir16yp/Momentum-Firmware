
#define FURI_PACKED
#define FURI_CONST_ASSIGN(dst, src) ((dst) = (src))
#define FURI_CONST_ASSIGN_PTR(dst, src) ((dst) = (src))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ICONS_FMT "Icons/%s/%s"
#define FONTS_FMT "Fonts/%s/%s"
#define U8G2_FONT_DATA_STRUCT_SIZE 23
enum { FSAM_READ, FSOM_OPEN_EXISTING };
typedef int Font;
typedef int FuriString;
typedef int File;
typedef struct {
    uint16_t width, height;
    uint8_t frame_count, frame_rate;
    uint8_t **frames;
} Icon;
typedef struct {
    int leading_default, leading_min, height, descender;
} CanvasFontParameters;
typedef struct {
    const Icon *original;
    const Icon *replaced;
} IconSwap;
typedef struct {
    int icons;
    uint8_t *fonts[1];
    CanvasFontParameters *font_params[1];
} AssetPacks;
static AssetPacks pack;
static AssetPacks *asset_packs = &pack;
static const struct {
    const char *asset_pack;
} momentum_settings = {"test"};
static int kind;
static int32_t metadata[4];
static uint64_t file_size;
static size_t allocations, releases, reads;
static const Icon *replacement;
static void furi_string_printf(FuriString *path, const char *format, ...)
{
    (void)path;
    kind = strstr(format, "/meta")     ? 0
           : strstr(format, "/frame_") ? 1
           : strstr(format, ".bmx")    ? 2
                                       : 3;
    reads = 0;
}
static const char *furi_string_get_cstr(FuriString *path)
{
    (void)path;
    return "test";
}
static bool storage_file_open(File *file, const char *path, int mode, int open_mode)
{
    (void)file;
    (void)path;
    (void)mode;
    (void)open_mode;
    return true;
}
static void storage_file_close(File *file)
{
    (void)file;
}
static uint64_t storage_file_size(File *file)
{
    (void)file;
    return file_size;
}
static size_t storage_file_read(File *file, void *data, size_t size)
{
    (void)file;
    if (kind == 0 || (kind == 2 && reads == 0))
        memcpy(data, metadata, size);
    else
        memset(data, 0x5a, size);
    ++reads;
    return size;
}
static void IconSwapList_push_back(int list, IconSwap swap)
{
    (void)list;
    replacement = swap.replaced;
}
static void *tracked_malloc(size_t size)
{
    assert(size > 0 && size < 4096);
    ++allocations;
    return calloc(1, size);
}
static void tracked_free(void *p)
{
    if (p)
        ++releases;
    free(p);
}
#define malloc tracked_malloc
#define free tracked_free
/* PRODUCTION */
#undef malloc
#undef free

int main(void)
{
    FuriString path = 0;
    File file = 0;
    Icon original = {0};
    metadata[0] = 16;
    metadata[1] = 16;
    metadata[2] = 10;
    const int32_t bad_counts[] = {-1, 0, 256, INT32_MAX};
    for (size_t i = 0; i < sizeof(bad_counts) / sizeof(bad_counts[0]); ++i) {
        metadata[3] = bad_counts[i];
        load_icon_animated(&original, "test", &path, &file);
        assert(!replacement && allocations == 0);
    }
    metadata[3] = 2;
    file_size = 0;
    load_icon_animated(&original, "test", &path, &file);
    assert(!replacement && allocations == releases);
    file_size = 4;
    load_icon_animated(&original, "test", &path, &file);
    assert(replacement && replacement->frame_count == 2 && replacement->frames[1][3] == 0x5a);
    free_icon(replacement);
    replacement = NULL;
    assert(allocations == releases);

    size_t before = allocations;
    for (file_size = 0; file_size <= sizeof(StaticIconBmxHeader); ++file_size) {
        load_icon_static(&original, "test", &path, &file);
        assert(!replacement && allocations == before);
    }
    file_size = UINT64_MAX;
    load_icon_static(&original, "test", &path, &file);
    assert(!replacement && allocations == before);
    file_size = sizeof(StaticIconBmxHeader) + 4;
    metadata[0] = -1;
    load_icon_static(&original, "test", &path, &file);
    assert(!replacement && allocations == before);
    metadata[0] = 16;
    load_icon_static(&original, "test", &path, &file);
    assert(replacement && replacement->frame_count == 1 && replacement->frames[0][3] == 0x5a);
    free_icon(replacement);
    replacement = NULL;
    assert(allocations == releases);

    before = allocations;
    for (file_size = 0; file_size <= U8G2_FONT_DATA_STRUCT_SIZE; ++file_size) {
        load_font(0, "test", &path, &file);
        assert(!pack.fonts[0] && allocations == before);
    }
    file_size = 24;
    load_font(0, "test", &path, &file);
    assert(pack.fonts[0] && pack.fonts[0][23] == 0x5a && pack.font_params[0]);
    free_font(0);
    assert(allocations == releases);
    return 0;
}
