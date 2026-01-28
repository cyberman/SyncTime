# Reaction UI Migration Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace GadTools-based preferences window with Reaction (BOOPSI) controls for a more modern AmigaOS 3.2+ UI.

**Architecture:** Single WindowObject with nested LayoutObjects. Chooser dropdown for region selection, ListBrowser for cities and log. Log panel toggles visibility dynamically.

**Tech Stack:** AmigaOS 3.2+ (LIB_VERSION 44), Reaction classes (window.class, layout.gadget, button.gadget, string.gadget, integer.gadget, chooser.gadget, listbrowser.gadget, label.image)

---

## Task 1: Update synctime.h for Reaction

**Files:**
- Modify: `include/synctime.h`

**Changes:**

1. Update LIB_VERSION from 39 to 44:
```c
#define LIB_VERSION 44  /* AmigaOS 3.2 minimum */
```

2. Add Reaction includes after existing includes:
```c
/* Reaction includes */
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/integer.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>

#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/integer.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/label.h>
```

3. Add library base externs:
```c
extern struct Library *WindowBase;
extern struct Library *LayoutBase;
extern struct Library *ButtonBase;
extern struct Library *StringBase;
extern struct Library *IntegerBase;
extern struct Library *ChooserBase;
extern struct Library *ListBrowserBase;
extern struct Library *LabelBase;
```

**Commit message:** `refactor: update synctime.h for Reaction UI (AmigaOS 3.2+)`

---

## Task 2: Update main.c Library Handling

**Files:**
- Modify: `src/main.c`

**Changes:**

1. Add library base definitions after existing ones:
```c
struct Library *WindowBase      = NULL;
struct Library *LayoutBase      = NULL;
struct Library *ButtonBase      = NULL;
struct Library *StringBase      = NULL;
struct Library *IntegerBase     = NULL;
struct Library *ChooserBase     = NULL;
struct Library *ListBrowserBase = NULL;
struct Library *LabelBase       = NULL;
```

2. Update `open_libraries()` to open Reaction classes:
```c
WindowBase = OpenLibrary("window.class", LIB_VERSION);
if (!WindowBase) return FALSE;

LayoutBase = OpenLibrary("gadgets/layout.gadget", LIB_VERSION);
if (!LayoutBase) return FALSE;

ButtonBase = OpenLibrary("gadgets/button.gadget", LIB_VERSION);
if (!ButtonBase) return FALSE;

StringBase = OpenLibrary("gadgets/string.gadget", LIB_VERSION);
if (!StringBase) return FALSE;

IntegerBase = OpenLibrary("gadgets/integer.gadget", LIB_VERSION);
if (!IntegerBase) return FALSE;

ChooserBase = OpenLibrary("gadgets/chooser.gadget", LIB_VERSION);
if (!ChooserBase) return FALSE;

ListBrowserBase = OpenLibrary("gadgets/listbrowser.gadget", LIB_VERSION);
if (!ListBrowserBase) return FALSE;

LabelBase = OpenLibrary("images/label.image", LIB_VERSION);
if (!LabelBase) return FALSE;
```

3. Update `close_libraries()` to close Reaction classes (reverse order):
```c
if (LabelBase) { CloseLibrary(LabelBase); LabelBase = NULL; }
if (ListBrowserBase) { CloseLibrary(ListBrowserBase); ListBrowserBase = NULL; }
if (ChooserBase) { CloseLibrary(ChooserBase); ChooserBase = NULL; }
if (IntegerBase) { CloseLibrary(IntegerBase); IntegerBase = NULL; }
if (StringBase) { CloseLibrary(StringBase); StringBase = NULL; }
if (ButtonBase) { CloseLibrary(ButtonBase); ButtonBase = NULL; }
if (LayoutBase) { CloseLibrary(LayoutBase); LayoutBase = NULL; }
if (WindowBase) { CloseLibrary(WindowBase); WindowBase = NULL; }
```

**Commit message:** `refactor: add Reaction library open/close in main.c`

---

## Task 3: Rewrite window.c with Reaction - Part 1 (Structure)

**Files:**
- Modify: `src/window.c`

**Changes:**

1. Remove all GadTools includes and replace with Reaction:
```c
#include "synctime.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
```

2. Replace gadget IDs:
```c
#define GID_STATUS      1
#define GID_LAST_SYNC   2
#define GID_NEXT_SYNC   3
#define GID_SERVER      4
#define GID_INTERVAL    5
#define GID_REGION      6
#define GID_CITY        7
#define GID_TZ_INFO     8
#define GID_SYNC        9
#define GID_SAVE        10
#define GID_HIDE        11
#define GID_LOG_TOGGLE  12
#define GID_LOG         13
```

3. Replace static state variables:
```c
static Object *window_obj = NULL;
static struct Window *win = NULL;

/* Gadget object pointers */
static Object *gad_status    = NULL;
static Object *gad_last_sync = NULL;
static Object *gad_next_sync = NULL;
static Object *gad_server    = NULL;
static Object *gad_interval  = NULL;
static Object *gad_region    = NULL;
static Object *gad_city      = NULL;
static Object *gad_tz_info   = NULL;
static Object *gad_log       = NULL;
static Object *gad_log_toggle = NULL;

/* Layout objects for dynamic log panel */
static Object *layout_root   = NULL;
static Object *layout_log    = NULL;

/* Log visibility state */
static BOOL log_visible = FALSE;

/* Timezone selection state */
static ULONG current_region_idx = 0;
static ULONG current_city_idx = 0;
static const TZEntry **current_cities = NULL;
static ULONG current_city_count = 0;

/* Lists for Reaction gadgets */
static struct List region_list;
static struct List city_list;
static struct List log_list;
```

4. Add helper functions for ListBrowser node management:
```c
/* ChooserNode for region dropdown */
static struct Node *region_nodes = NULL;

/* ListBrowserNode structures for city and log lists */
static void free_list_nodes(struct List *list);
static void build_region_chooser_list(void);
static void build_city_browser_list(const char *region);
```

**Commit message:** `refactor: window.c structure for Reaction UI`

---

## Task 4: Rewrite window.c with Reaction - Part 2 (Window Creation)

**Files:**
- Modify: `src/window.c`

**Implement window_open():**

```c
BOOL window_open(struct Screen *screen)
{
    SyncConfig *cfg;
    const char **regions;
    ULONG region_count;

    (void)screen;

    if (window_obj)
        return TRUE;  /* Already open */

    cfg = config_get();

    /* Initialize timezone selection from config */
    /* ... find region/city indices from cfg->tz_name ... */

    /* Build chooser list for regions */
    build_region_chooser_list();

    /* Build listbrowser list for cities */
    build_city_browser_list(regions[current_region_idx]);

    /* Create window object */
    window_obj = WindowObject,
        WA_Title, "SyncTime",
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate, TRUE,
        WA_SizeGadget, FALSE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, layout_root = VLayoutObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_BevelStyle, BVS_THIN,

            /* Status group */
            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_BevelStyle, BVS_GROUP,
                LAYOUT_Label, "Status",
                /* ... status gadgets ... */
            LayoutEnd,

            /* Settings group */
            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_BevelStyle, BVS_GROUP,
                LAYOUT_Label, "Settings",
                /* ... server, interval gadgets ... */
            LayoutEnd,

            /* Timezone group */
            LAYOUT_AddChild, VLayoutObject,
                LAYOUT_BevelStyle, BVS_GROUP,
                LAYOUT_Label, "Timezone",
                /* ... region, city, tz_info gadgets ... */
            LayoutEnd,

            /* Button row */
            LAYOUT_AddChild, HLayoutObject,
                LAYOUT_EvenSize, TRUE,
                /* ... buttons ... */
            LayoutEnd,

        LayoutEnd,
    WindowEnd;

    if (!window_obj)
        return FALSE;

    win = RA_OpenWindow(window_obj);
    if (!win) {
        DisposeObject(window_obj);
        window_obj = NULL;
        return FALSE;
    }

    return TRUE;
}
```

**Commit message:** `feat: implement Reaction window_open()`

---

## Task 5: Rewrite window.c with Reaction - Part 3 (Event Handling)

**Files:**
- Modify: `src/window.c`

**Implement window_handle_events():**

```c
BOOL window_handle_events(SyncConfig *cfg, SyncStatus *st)
{
    ULONG result;
    WORD code;
    BOOL sync_requested = FALSE;

    (void)st;

    if (!window_obj || !win)
        return FALSE;

    while ((result = RA_HandleInput(window_obj, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                window_close();
                return sync_requested;

            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                    case GID_SYNC:
                        sync_requested = TRUE;
                        break;

                    case GID_SAVE:
                        /* Read gadget values and save config */
                        save_config_from_gadgets(cfg);
                        break;

                    case GID_HIDE:
                        window_close();
                        return sync_requested;

                    case GID_LOG_TOGGLE:
                        toggle_log_panel();
                        break;

                    case GID_REGION:
                        handle_region_change(code);
                        break;

                    case GID_CITY:
                        handle_city_change(code);
                        break;
                }
                break;
        }
    }

    return sync_requested;
}
```

**Helper functions:**
- `toggle_log_panel()` - Add/remove log layout, resize window
- `handle_region_change(code)` - Rebuild city list, update display
- `handle_city_change(code)` - Update TZ info text
- `save_config_from_gadgets(cfg)` - Read values, call config setters

**Commit message:** `feat: implement Reaction event handling`

---

## Task 6: Rewrite window.c with Reaction - Part 4 (Updates & Helpers)

**Files:**
- Modify: `src/window.c`

**Implement remaining functions:**

```c
void window_close(void)
{
    if (win) {
        RA_CloseWindow(window_obj);
        win = NULL;
    }
    if (window_obj) {
        DisposeObject(window_obj);
        window_obj = NULL;
    }

    /* Free list nodes */
    free_list_nodes(&region_list);
    free_list_nodes(&city_list);
    free_list_nodes(&log_list);

    /* Reset state */
    layout_root = layout_log = NULL;
    gad_status = gad_last_sync = gad_next_sync = NULL;
    gad_server = gad_interval = NULL;
    gad_region = gad_city = gad_tz_info = NULL;
    gad_log = gad_log_toggle = NULL;
    log_visible = FALSE;
}

BOOL window_is_open(void)
{
    return (win != NULL);
}

ULONG window_signal(void)
{
    if (win)
        return 1UL << win->UserPort->mp_SigBit;
    return 0;
}

void window_update_status(SyncStatus *st)
{
    if (!win) return;

    SetGadgetAttrs((struct Gadget *)gad_status, win, NULL,
        GA_Text, st->status_text, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)gad_last_sync, win, NULL,
        GA_Text, st->last_sync_text, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)gad_next_sync, win, NULL,
        GA_Text, st->next_sync_text, TAG_DONE);
}

void window_log(const char *message)
{
    /* Add entry to log ListBrowser */
    /* Auto-scroll to bottom */
}
```

**Commit message:** `feat: implement Reaction window helpers and updates`

---

## Task 7: Build and Test

**Steps:**

1. Clean and rebuild:
```bash
make clean
make
```

2. Verify no compiler warnings/errors

3. Test on AmigaOS 3.2+ (or emulator):
   - Window opens with correct layout
   - Region dropdown shows all regions
   - City list updates on region change
   - TZ info updates on city selection
   - Log toggle shows/hides log panel
   - Save persists settings
   - Sync Now triggers sync

**Commit message:** `test: verify Reaction UI build and functionality`

---

## Summary

| Task | Description |
|------|-------------|
| 1 | Update synctime.h for Reaction includes and library bases |
| 2 | Update main.c to open/close Reaction libraries |
| 3 | Rewrite window.c structure (state, IDs, helpers) |
| 4 | Implement Reaction window_open() |
| 5 | Implement Reaction event handling |
| 6 | Implement window helpers and update functions |
| 7 | Build and test |
