/* window.c - GadTools configuration/status window for SyncTime
 *
 * Opens a GadTools window on the default public screen (Workbench)
 * showing sync status and editable configuration fields.
 * Opened via Exchange "Show" or the commodity hotkey.
 */

#include "synctime.h"

/* =========================================================================
 * Gadget IDs
 * ========================================================================= */

#define GID_STATUS     0
#define GID_LAST_SYNC  1
#define GID_NEXT_SYNC  2
#define GID_LOG        3
#define GID_SERVER     4
#define GID_INTERVAL   5
#define GID_REGION     6
#define GID_CITY       7
#define GID_TZ_INFO    8
#define GID_SYNC       9
#define GID_SAVE       10
#define GID_HIDE       11

/* Log system */
#define LOG_MAX_ENTRIES 50
#define LOG_LINE_LEN    64

/* =========================================================================
 * Static module state
 * ========================================================================= */

static struct Window *win   = NULL;
static struct Gadget *glist = NULL;
static APTR vi              = NULL;   /* VisualInfo */

/* Individual gadget pointers for updating */
static struct Gadget *gad_status    = NULL;
static struct Gadget *gad_last_sync = NULL;
static struct Gadget *gad_next_sync = NULL;
static struct Gadget *gad_log       = NULL;
static struct Gadget *gad_server    = NULL;
static struct Gadget *gad_interval  = NULL;
static struct Gadget *gad_region    = NULL;
static struct Gadget *gad_city      = NULL;
static struct Gadget *gad_tz_info   = NULL;

/* Local edit state */
static BOOL config_changed  = FALSE;

/* Region/City picker state */
static ULONG current_region_idx = 0;
static ULONG current_city_idx = 0;

/* City list for LISTVIEW_KIND */
static struct List city_list_header;
static struct Node city_nodes[200];
static ULONG city_node_count = 0;
static const TZEntry **current_cities = NULL;
static ULONG current_city_count = 0;

/* Log entries stored as Exec List of Node structures */
static struct List log_list;
static LONG log_count = 0;

/* Log node structure - Node followed by text buffer */
struct LogNode {
    struct Node node;
    char text[LOG_LINE_LEN];
};

static struct LogNode log_nodes[LOG_MAX_ENTRIES];
static LONG log_next_slot = 0;

/* =========================================================================
 * Helper functions for region/city picker
 * ========================================================================= */

/* Build the city list for a given region */
static void build_city_list(const char *region)
{
    ULONG i;
    current_cities = tz_get_cities_for_region(region, &current_city_count);
    NewList(&city_list_header);
    city_node_count = 0;
    for (i = 0; i < current_city_count && i < 200; i++) {
        city_nodes[i].ln_Name = (STRPTR)current_cities[i]->city;
        city_nodes[i].ln_Type = 0;
        city_nodes[i].ln_Pri = 0;
        AddTail(&city_list_header, &city_nodes[i]);
        city_node_count++;
    }
}

/* Buffer for timezone info display */
static char tz_info_buf[64];

/* Format timezone info string for display */
static void format_tz_info(const TZEntry *tz)
{
    LONG offset_mins_rem, offset_hrs;
    char sign;
    char *p;

    if (tz == NULL) {
        strcpy(tz_info_buf, "UTC");
        return;
    }

    offset_mins_rem = tz->std_offset_mins;
    sign = (offset_mins_rem >= 0) ? '+' : '-';
    if (offset_mins_rem < 0) offset_mins_rem = -offset_mins_rem;
    offset_hrs = offset_mins_rem / 60;
    offset_mins_rem = offset_mins_rem % 60;

    p = tz_info_buf;

    /* Build "UTC+X" or "UTC+X:MM" */
    *p++ = 'U'; *p++ = 'T'; *p++ = 'C'; *p++ = sign;

    /* Convert hours to string */
    if (offset_hrs >= 10) {
        *p++ = '0' + (offset_hrs / 10);
    }
    *p++ = '0' + (offset_hrs % 10);

    if (offset_mins_rem > 0) {
        *p++ = ':';
        *p++ = '0' + (offset_mins_rem / 10);
        *p++ = '0' + (offset_mins_rem % 10);
    }

    /* Add DST info */
    if (tz->dst_offset_mins > 0) {
        /* Has DST */
        strcpy(p, ", DST active seasonally");
    } else {
        strcpy(p, " (no DST)");
    }
}

/* =========================================================================
 * window_open -- create and display the GadTools configuration window
 * ========================================================================= */

/* Initialize log list (called once) */
static BOOL log_initialized = FALSE;
static void init_log_list(void)
{
    LONG i;
    if (log_initialized)
        return;
    NewList(&log_list);
    for (i = 0; i < LOG_MAX_ENTRIES; i++) {
        log_nodes[i].node.ln_Succ = NULL;
        log_nodes[i].node.ln_Pred = NULL;
        log_nodes[i].text[0] = '\0';
    }
    log_next_slot = 0;
    log_count = 0;
    log_initialized = TRUE;
}

BOOL window_open(struct Screen *screen)
{
    struct Screen *pub;
    struct TextAttr *font;
    UWORD fonth, topoff, leftoff, label_width, gad_left, gad_width, win_width;
    UWORD spacing, y;
    struct Gadget *gad;
    struct NewGadget ng;
    SyncConfig *cfg;

    (void)screen;  /* Not used -- we lock the default public screen */

    /* Initialize log list if needed */
    init_log_list();

    if (win)
        return TRUE;   /* Already open */

    /* Read current config so gadgets reflect live values */
    cfg = config_get();
    config_changed = FALSE;

    /* Find current timezone in table and set up region/city indices */
    {
        const TZEntry *tz;
        const char **regions;
        ULONG region_count, i;

        regions = tz_get_regions(&region_count);
        tz = tz_find_by_name(cfg->tz_name);

        if (tz) {
            /* Find region index */
            for (i = 0; i < region_count; i++) {
                if (strcmp(regions[i], tz->region) == 0) {
                    current_region_idx = i;
                    break;
                }
            }
            /* Build city list and find city index */
            build_city_list(tz->region);
            for (i = 0; i < current_city_count; i++) {
                if (strcmp(current_cities[i]->name, cfg->tz_name) == 0) {
                    current_city_idx = i;
                    break;
                }
            }
        } else {
            current_region_idx = 0;
            build_city_list(regions[0]);
            current_city_idx = 0;
        }
    }

    /* Lock default public screen (Workbench) */
    pub = LockPubScreen(NULL);
    if (!pub)
        return FALSE;

    vi = GetVisualInfo(pub, TAG_DONE);
    if (!vi) {
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    /* Font-relative sizing */
    font       = pub->Font;
    fonth      = font->ta_YSize;
    topoff     = pub->WBorTop + fonth + 1;
    leftoff    = pub->WBorLeft + 4;
    label_width = 80;
    gad_left   = leftoff + label_width;
    gad_width  = 220;
    win_width  = gad_left + gad_width + pub->WBorRight + 8;
    spacing    = fonth + 6;
    y          = topoff + 4;

    /* Create gadget context */
    gad = CreateContext(&glist);
    if (!gad) {
        FreeVisualInfo(vi);
        vi = NULL;
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr   = font;

    /* ---- Status (TEXT_KIND) ---- */
    ng.ng_LeftEdge   = gad_left;
    ng.ng_TopEdge    = y;
    ng.ng_Width      = gad_width;
    ng.ng_Height     = fonth + 4;
    ng.ng_GadgetText = "Status:";
    ng.ng_GadgetID   = GID_STATUS;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_status = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Text,   (ULONG)"Idle",
        GTTX_Border, TRUE,
        TAG_DONE);
    y += spacing;

    /* ---- Last sync (TEXT_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Last sync:";
    ng.ng_GadgetID   = GID_LAST_SYNC;
    gad_last_sync = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Text,   (ULONG)"Never",
        GTTX_Border, TRUE,
        TAG_DONE);
    y += spacing;

    /* ---- Next sync (TEXT_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Next sync:";
    ng.ng_GadgetID   = GID_NEXT_SYNC;
    gad_next_sync = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Text,   (ULONG)"Pending",
        GTTX_Border, TRUE,
        TAG_DONE);
    y += spacing;

    /* Extra gap before log */
    y += 4;

    /* ---- Log (LISTVIEW_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Log:";
    ng.ng_GadgetID   = GID_LOG;
    ng.ng_Height     = fonth * 5 + 4;  /* 5 lines visible */
    gad_log = gad = CreateGadget(LISTVIEW_KIND, gad, &ng,
        GTLV_Labels,     (ULONG)&log_list,
        GTLV_ReadOnly,   TRUE,
        GTLV_ScrollWidth, 16,
        TAG_DONE);
    y += ng.ng_Height + 4;

    /* Reset height for other gadgets */
    ng.ng_Height = fonth + 4;

    /* Extra gap before editable section */
    y += 4;

    /* ---- Server (STRING_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Server:";
    ng.ng_GadgetID   = GID_SERVER;
    gad_server = gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String,   (ULONG)cfg->server,
        GTST_MaxChars, SERVER_NAME_MAX - 1,
        TAG_DONE);
    y += spacing;

    /* ---- Interval (INTEGER_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Interval:";
    ng.ng_GadgetID   = GID_INTERVAL;
    gad_interval = gad = CreateGadget(INTEGER_KIND, gad, &ng,
        GTIN_Number,   cfg->interval,
        GTIN_MaxChars, 6,
        TAG_DONE);
    y += spacing;

    /* ---- Region (CYCLE_KIND) ---- */
    {
        const char **regions = tz_get_regions(NULL);
        ng.ng_TopEdge    = y;
        ng.ng_Width      = gad_width;
        ng.ng_GadgetText = "Region:";
        ng.ng_GadgetID   = GID_REGION;
        ng.ng_Flags      = PLACETEXT_LEFT;
        gad_region = gad = CreateGadget(CYCLE_KIND, gad, &ng,
            GTCY_Labels, (ULONG)regions,
            GTCY_Active, current_region_idx,
            TAG_DONE);
        y += spacing;
    }

    /* ---- City (LISTVIEW_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_Height     = fonth * 5 + 4;  /* 5 lines visible */
    ng.ng_GadgetText = "City:";
    ng.ng_GadgetID   = GID_CITY;
    gad_city = gad = CreateGadget(LISTVIEW_KIND, gad, &ng,
        GTLV_Labels, (ULONG)&city_list_header,
        GTLV_ShowSelected, (ULONG)NULL,
        GTLV_Selected, current_city_idx,
        GTLV_ScrollWidth, 16,
        TAG_DONE);
    y += ng.ng_Height + 4;
    ng.ng_Height = fonth + 4;  /* Reset height */

    /* ---- TZ Info (TEXT_KIND) ---- */
    if (current_city_count > 0 && current_city_idx < current_city_count)
        format_tz_info(current_cities[current_city_idx]);
    else
        format_tz_info(NULL);
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID   = GID_TZ_INFO;
    ng.ng_Flags      = 0;
    gad_tz_info = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Text, (ULONG)tz_info_buf,
        GTTX_Border, TRUE,
        TAG_DONE);
    y += spacing;

    /* Extra gap before buttons */
    y += 10;

    /* Button row: Sync Now, Save, Hide (3 buttons with 5px gaps) */
    {
        UWORD btn_width = (gad_width - 10) / 3;  /* 3 buttons, 2 gaps of 5px */
        UWORD btn_gap = 5;

        ng.ng_TopEdge    = y;
        ng.ng_Height     = fonth + 6;
        ng.ng_Flags      = PLACETEXT_IN;

        /* ---- Sync Now button ---- */
        ng.ng_LeftEdge   = gad_left;
        ng.ng_Width      = btn_width;
        ng.ng_GadgetText = "Sync Now";
        ng.ng_GadgetID   = GID_SYNC;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

        /* ---- Save button ---- */
        ng.ng_LeftEdge   = gad_left + btn_width + btn_gap;
        ng.ng_GadgetText = "Save";
        ng.ng_GadgetID   = GID_SAVE;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

        /* ---- Hide button ---- */
        ng.ng_LeftEdge   = gad_left + 2 * (btn_width + btn_gap);
        ng.ng_GadgetText = "Hide";
        ng.ng_GadgetID   = GID_HIDE;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

        y += ng.ng_Height;
    }

    if (!gad) {
        /* One or more gadgets failed to create */
        FreeGadgets(glist);
        glist = NULL;
        FreeVisualInfo(vi);
        vi = NULL;
        gad_status = gad_last_sync = gad_next_sync = gad_log = NULL;
        gad_server = gad_interval = NULL;
        gad_region = gad_city = gad_tz_info = NULL;
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    /* Open the window */
    win = OpenWindowTags(NULL,
        WA_Left,        100,
        WA_Top,         50,
        WA_Width,       win_width,
        WA_Height,      y + fonth + 8 + pub->WBorBottom,
        WA_Title,       (ULONG)"SyncTime",
        WA_PubScreen,   (ULONG)pub,
        WA_Gadgets,     (ULONG)glist,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                        BUTTONIDCMP | STRINGIDCMP | CYCLEIDCMP |
                        LISTVIEWIDCMP,
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate,    TRUE,
        WA_RMBTrap,     TRUE,
        TAG_DONE);

    if (!win) {
        FreeGadgets(glist);
        glist = NULL;
        FreeVisualInfo(vi);
        vi = NULL;
        gad_status = gad_last_sync = gad_next_sync = gad_log = NULL;
        gad_server = gad_interval = NULL;
        gad_region = gad_city = gad_tz_info = NULL;
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    GT_RefreshWindow(win, NULL);
    UnlockPubScreen(NULL, pub);

    return TRUE;
}

/* =========================================================================
 * window_close -- tear down window and free all GadTools resources
 * ========================================================================= */

void window_close(void)
{
    if (win) {
        CloseWindow(win);
        win = NULL;
    }
    if (glist) {
        FreeGadgets(glist);
        glist = NULL;
    }
    if (vi) {
        FreeVisualInfo(vi);
        vi = NULL;
    }

    gad_status    = NULL;
    gad_last_sync = NULL;
    gad_next_sync = NULL;
    gad_log       = NULL;
    gad_server    = NULL;
    gad_interval  = NULL;
    gad_region    = NULL;
    gad_city      = NULL;
    gad_tz_info   = NULL;
}

/* =========================================================================
 * window_is_open -- query whether the window is currently displayed
 * ========================================================================= */

BOOL window_is_open(void)
{
    return (win != NULL);
}

/* =========================================================================
 * window_handle_events -- process all pending Intuition/GadTools messages
 *
 * cfg: pointer to live config struct (updated on Save)
 * st:  pointer to sync status (currently unused here, reserved for future)
 *
 * Returns TRUE if "Sync Now" was pressed, FALSE otherwise.
 * ========================================================================= */

BOOL window_handle_events(SyncConfig *cfg, SyncStatus *st)
{
    struct IntuiMessage *msg;
    BOOL sync_requested = FALSE;

    (void)st;  /* status is updated via window_update_status */

    if (!win)
        return FALSE;

    while ((msg = GT_GetIMsg(win->UserPort))) {
        ULONG class = msg->Class;
        UWORD code  = msg->Code;
        struct Gadget *gad = (struct Gadget *)msg->IAddress;

        GT_ReplyIMsg(msg);

        switch (class) {
            case IDCMP_CLOSEWINDOW:
                window_close();
                return sync_requested;  /* Window is gone -- stop processing */

            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
                break;

            case IDCMP_GADGETUP:
                switch (gad->GadgetID) {
                    case GID_SYNC:
                        sync_requested = TRUE;
                        break;

                    case GID_SAVE: {
                        /* Read current gadget values and push into config */
                        char *srv;
                        LONG intv;

                        srv  = ((struct StringInfo *)gad_server->SpecialInfo)->Buffer;
                        intv = ((struct StringInfo *)gad_interval->SpecialInfo)->LongInt;

                        config_set_server(srv);
                        config_set_interval(intv);
                        /* Set timezone from current city selection */
                        if (current_city_count > 0 && current_city_idx < current_city_count) {
                            config_set_tz_name(current_cities[current_city_idx]->name);
                        }
                        config_save();
                        config_changed = TRUE;

                        /* cfg already points to the static config struct
                         * returned by config_get(), so it's already updated.
                         * No copy needed.
                         */
                        (void)cfg;
                        break;
                    }

                    case GID_HIDE:
                        window_close();
                        return sync_requested;

                    case GID_REGION: {
                        const char **regions = tz_get_regions(NULL);
                        current_region_idx = code;
                        /* Detach list before modification */
                        GT_SetGadgetAttrs(gad_city, win, NULL,
                            GTLV_Labels, (ULONG)~0,
                            TAG_DONE);
                        build_city_list(regions[current_region_idx]);
                        current_city_idx = 0;
                        GT_SetGadgetAttrs(gad_city, win, NULL,
                            GTLV_Labels, (ULONG)&city_list_header,
                            GTLV_Selected, 0,
                            TAG_DONE);
                        if (current_city_count > 0) {
                            format_tz_info(current_cities[0]);
                            GT_SetGadgetAttrs(gad_tz_info, win, NULL,
                                GTTX_Text, (ULONG)tz_info_buf, TAG_DONE);
                        }
                        break;
                    }

                    case GID_CITY:
                        if (code < current_city_count) {
                            current_city_idx = code;
                            format_tz_info(current_cities[code]);
                            GT_SetGadgetAttrs(gad_tz_info, win, NULL,
                                GTTX_Text, (ULONG)tz_info_buf, TAG_DONE);
                        }
                        break;
                }
                break;
        }
    }

    return sync_requested;
}

/* =========================================================================
 * window_signal -- return the signal mask for the window's message port
 * ========================================================================= */

ULONG window_signal(void)
{
    if (win)
        return 1UL << win->UserPort->mp_SigBit;
    return 0;
}

/* =========================================================================
 * window_update_status -- refresh the three read-only text gadgets
 * ========================================================================= */

void window_update_status(SyncStatus *st)
{
    if (!win)
        return;

    GT_SetGadgetAttrs(gad_status, win, NULL,
        GTTX_Text, (ULONG)st->status_text, TAG_DONE);
    GT_SetGadgetAttrs(gad_last_sync, win, NULL,
        GTTX_Text, (ULONG)st->last_sync_text, TAG_DONE);
    GT_SetGadgetAttrs(gad_next_sync, win, NULL,
        GTTX_Text, (ULONG)st->next_sync_text, TAG_DONE);
}

/* =========================================================================
 * window_log -- add an entry to the scrollable log
 * ========================================================================= */

void window_log(const char *message)
{
    struct LogNode *node;
    LONG i;

    /* Initialize log list if needed (may be called before window_open) */
    init_log_list();

    /* Get next slot (circular buffer) */
    node = &log_nodes[log_next_slot];

    /* If this node is already in the list, remove it */
    if (node->node.ln_Succ != NULL && node->node.ln_Pred != NULL) {
        Remove(&node->node);
    }

    /* Copy message text */
    for (i = 0; i < LOG_LINE_LEN - 1 && message[i] != '\0'; i++) {
        node->text[i] = message[i];
    }
    node->text[i] = '\0';

    /* Set up node */
    node->node.ln_Name = node->text;
    node->node.ln_Type = 0;
    node->node.ln_Pri = 0;

    /* Add to end of list */
    AddTail(&log_list, &node->node);

    /* Advance slot */
    log_next_slot = (log_next_slot + 1) % LOG_MAX_ENTRIES;
    if (log_count < LOG_MAX_ENTRIES)
        log_count++;

    /* Update listview if window is open */
    if (win && gad_log) {
        GT_SetGadgetAttrs(gad_log, win, NULL,
            GTLV_Labels, (ULONG)&log_list,
            GTLV_Top, log_count > 5 ? log_count - 5 : 0,  /* Auto-scroll to bottom */
            TAG_DONE);
    }
}
