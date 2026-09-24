#include "str_buffer.h"

const char *str_buffer_make_owned_clone(StrBuffer *buffer, const char *str)
{
    furi_check(buffer->n_owned_strings < SIZE_MAX / sizeof(*buffer->owned_strings));
    size_t count = buffer->n_owned_strings + 1;
    char **strings = realloc(buffer->owned_strings, count * sizeof(*strings));
    furi_check(strings);
    buffer->owned_strings = strings;
    char *owned = strdup(str);
    furi_check(owned);
    buffer->owned_strings[buffer->n_owned_strings] = owned;
    buffer->n_owned_strings = count;
    return owned;
}

void str_buffer_clear_all_clones(StrBuffer *buffer)
{
    for (size_t i = 0; i < buffer->n_owned_strings; i++) {
        free(buffer->owned_strings[i]);
    }
    free(buffer->owned_strings);
    buffer->owned_strings = NULL;
    buffer->n_owned_strings = 0;
}
