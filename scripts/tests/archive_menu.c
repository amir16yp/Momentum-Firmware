#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <m-array.h>

/* EVENTS */
typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyLeft,
    InputKeyRight,
    InputKeyOk,
    InputKeyBack
} InputKey;
typedef enum { InputTypeShort, InputTypeLong } InputType;
typedef struct {
    InputKey key;
    InputType type;
} InputEvent;
typedef struct {
    uint32_t event;
} ArchiveContextMenuItem_t;
ARRAY_DEF(menu_array, ArchiveContextMenuItem_t, M_POD_OPLIST)
typedef struct {
    menu_array_t context_menu;
    uint8_t menu_idx;
    bool menu_manage;
    bool menu_can_switch;
} ArchiveBrowserViewModel;
typedef struct {
    ArchiveBrowserViewModel *view;
    void (*callback)(uint32_t, void *);
    void *context;
} ArchiveBrowserView;

#define with_view_model(view, declaration, code, update)                                           \
    do {                                                                                           \
        declaration = (view);                                                                      \
        code;                                                                                      \
    } while (0)

static bool menu_input(ArchiveBrowserView *browser, InputEvent *event)
{
    bool in_menu = true;
    /* MENU_INPUT */
}

static unsigned callback_count;
static uint32_t last_event;
static void callback(uint32_t event, void *context)
{
    assert(context == &callback_count);
    callback_count++;
    last_event = event;
}
static void press(ArchiveBrowserView *browser, InputKey key)
{
    InputEvent input = {.key = key, .type = InputTypeShort};
    assert(menu_input(browser, &input));
}
static void populate(ArchiveBrowserViewModel *model)
{
    ArchiveContextMenuItem_t first = {.event = ArchiveBrowserEventFileMenuCopy};
    ArchiveContextMenuItem_t second = {.event = ArchiveBrowserEventFileMenuDelete};
    menu_array_push_back(model->context_menu, first);
    menu_array_push_back(model->context_menu, second);
}
static void press_empty(ArchiveBrowserView *browser)
{
    unsigned before = callback_count;
    press(browser, InputKeyOk);
    press(browser, InputKeyUp);
    press(browser, InputKeyDown);
    assert(callback_count == before);
}
int main(void)
{
    ArchiveBrowserViewModel model = {.menu_can_switch = true};
    menu_array_init(model.context_menu);
    ArchiveBrowserView browser = {.view = &model, .callback = callback, .context = &callback_count};

    // Opening a menu precedes its first draw: the array has no backing storage.
    press_empty(&browser);
    assert(model.menu_idx == 0);
    press(&browser, InputKeyBack);
    assert(last_event == ArchiveBrowserEventFileMenuClose);

    populate(&model);
    press(&browser, InputKeyUp);
    assert(model.menu_idx == 1);
    press(&browser, InputKeyOk);
    assert(last_event == ArchiveBrowserEventFileMenuDelete);
    press(&browser, InputKeyDown);
    assert(model.menu_idx == 0);
    press(&browser, InputKeyOk);
    assert(last_event == ArchiveBrowserEventFileMenuCopy);

    // Switching menus resets the entries before a subsequent draw.
    for (unsigned i = 0; i < 100; i++) {
        press(&browser, InputKeyRight);
        assert(model.menu_manage && menu_array_size(model.context_menu) == 0);
        press_empty(&browser);
        populate(&model);
        press(&browser, InputKeyLeft);
        assert(!model.menu_manage && menu_array_size(model.context_menu) == 0);
        press_empty(&browser);
        populate(&model);
    }

    // A stale selection must not dispatch an event outside the rebuilt menu.
    model.menu_idx = 2;
    unsigned before = callback_count;
    press(&browser, InputKeyOk);
    assert(callback_count == before);
    menu_array_clear(model.context_menu);
    puts("Archive menu: empty input, redraw gaps, selection bounds and navigation passed");
    return 0;
}
