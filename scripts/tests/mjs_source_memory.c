/* Exercise production file execution with tracked source-buffer ownership. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MJS_PRIVATE
#define MJS_ENABLE_DEBUG 0
#define MJS_GENERATE_JSC 0
#define MJS_UNDEFINED (-1)
typedef int mjs_val_t;
typedef enum { MJS_OK, MJS_FILE_READ_ERROR, MJS_SYNTAX_ERROR, MJS_TYPE_ERROR } mjs_err_t;
struct mjs {
    size_t bcode_len;
    mjs_err_t error;
    int generate_jsc;
};
static char *source_buffer;
static bool read_failure;
static mjs_err_t parse_result;
static mjs_err_t execute_result;
static unsigned frees, executions;

static char *cs_read_file(const char *path, size_t *size)
{
    assert(strcmp(path, "gui.js") == 0);
    if (read_failure)
        return NULL;
    *size = 16384;
    source_buffer = malloc(*size + 1);
    assert(source_buffer);
    memset(source_buffer, ' ', *size);
    source_buffer[*size] = 0;
    return source_buffer;
}
static void tracked_free(void *ptr)
{
    assert(ptr && ptr == source_buffer);
    frees++;
    source_buffer = NULL;
    free(ptr);
}
static mjs_err_t mjs_parse(const char *path, const char *src, struct mjs *mjs)
{
    assert(path && src);
    if (strcmp(path, "gui.js") == 0)
        assert(src == source_buffer);
    mjs->bcode_len += 128;
    return parse_result;
}
static void mjs_execute(struct mjs *mjs, size_t off, mjs_val_t *result)
{
    /* GUI/native allocations must not overlap the source allocation. */
    assert(source_buffer == NULL);
    assert(off == mjs->bcode_len - 128);
    executions++;
    *result = 42;
    mjs->error = execute_result;
}
static void mjs_prepend_errorf(struct mjs *mjs, mjs_err_t error, const char *fmt, const char *path)
{
    assert(fmt && path);
    mjs->error = error;
}
#define free tracked_free
/* PRODUCTION_CODE */
#undef free

int main(void)
{
    struct mjs mjs = {.bcode_len = 512};
    mjs_val_t result;
    assert(mjs_exec_file(&mjs, "gui.js", &result) == MJS_OK);
    assert(result == 42 && frees == 1 && executions == 1);
    execute_result = MJS_TYPE_ERROR;
    assert(mjs_exec_file(&mjs, "gui.js", &result) == MJS_TYPE_ERROR);
    assert(frees == 2 && executions == 2);
    parse_result = MJS_SYNTAX_ERROR;
    assert(mjs_exec_file(&mjs, "gui.js", &result) == MJS_SYNTAX_ERROR);
    assert(result == MJS_UNDEFINED && frees == 3 && executions == 2);
    read_failure = true;
    assert(mjs_exec_file(&mjs, "gui.js", &result) == MJS_FILE_READ_ERROR);
    assert(result == MJS_UNDEFINED && frees == 3 && executions == 2);
    read_failure = false;
    parse_result = execute_result = MJS_OK;
    assert(mjs_exec_file(&mjs, "gui.js", NULL) == MJS_OK);
    assert(frees == 4 && executions == 3);
    /* Caller-owned source must never be freed by mjs_exec. */
    const char borrowed[] = "42;";
    assert(mjs_exec(&mjs, borrowed, &result) == MJS_OK);
    assert(result == 42 && frees == 4 && executions == 4);
    assert(strcmp(borrowed, "42;") == 0);
    return 0;
}
