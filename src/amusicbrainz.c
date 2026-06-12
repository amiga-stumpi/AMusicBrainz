#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "amitcp13/bsdsocket.h"

#define APP_TITLE "AMusicBrainz v1.0 by Marcel Jaehne (c)2026"
#define MB_HOST "musicbrainz.org"
#define MB_PORT 80
#define CONFIG_FILE "AMusicBrainz.conf"

#define QUERY_SIZE 96
#define HTTP_REQ_SIZE 768
#define HTTP_BUF_SIZE 32768UL
#define RESULT_LINES 80
#define RESULT_LINE_SIZE 96
#define SEARCH_RESULT_LIMIT 20

#define GID_QUERY 1
#define GID_SEARCH 2
#define GID_ARTIST 3
#define GID_RELEASES 4
#define GID_TRACK 5
#define GID_SCROLL_UP 6
#define GID_SCROLL_DOWN 7
#define GID_INFO_OK 8

#define QUERY_LABEL_Y 23
#define QUERY_GADGET_Y 13
#define MODE_GADGET_Y 31
#define MODE_MARK_Y 41
#define SEPARATOR_Y 51
#define RESULT_BOX_Y 57
#define RESULT_TEXT_Y 71
#define RESULT_BOTTOM_PAD 77
#define RESULT_ROW_H 10

#define MODE_ARTIST 0
#define MODE_RELEASES 1
#define MODE_TRACK 2

#define HIT_ARTISTS 0
#define HIT_ALBUMS 1
#define HIT_TRACKS 2

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static struct Window *g_win;
static struct Library *g_sock_base;
static char *g_http_buf;
static char g_query[QUERY_SIZE] = "Queen";
static char g_query_undo[QUERY_SIZE];
static char g_status[96] = "Ready";
static char g_results[RESULT_LINES][RESULT_LINE_SIZE];
static char g_result_values[RESULT_LINES][QUERY_SIZE];
static char g_result_select[RESULT_LINES][QUERY_SIZE];
static char g_result_keys[RESULT_LINES][20];
static UWORD g_result_count;
static UWORD g_mode = MODE_ARTIST;
static UWORD g_hit_kind;
static UWORD g_main_top;
static UWORD g_results_truncated;
static char g_config_buf[512];

static void do_search(void);
static void redraw(void);
static void fetch_artist_albums(const char *artist_id, const char *artist_name);
static void fetch_album_tracks(const char *release_id, const char *release_title);
static UWORD main_visible_rows(void);

static struct StringInfo g_query_si = {
    (STRPTR)g_query, (STRPTR)g_query_undo, 0, QUERY_SIZE,
    0, 0, 0, 0, 0, 0, 0, 0, 0
};

static struct IntuiText g_search_text = { 1, 0, JAM2, 13, 2, 0, (UBYTE *)"Search", 0 };
static struct IntuiText g_artist_text = { 1, 0, JAM2, 16, 2, 0, (UBYTE *)"Artist", 0 };
static struct IntuiText g_releases_text = { 1, 0, JAM2, 10, 2, 0, (UBYTE *)"Releases", 0 };
static struct IntuiText g_track_text = { 1, 0, JAM2, 16, 2, 0, (UBYTE *)"Track", 0 };
static struct IntuiText g_scroll_up_text = { 1, 0, JAM2, 8, 2, 0, (UBYTE *)"Up", 0 };
static struct IntuiText g_scroll_down_text = { 1, 0, JAM2, 2, 2, 0, (UBYTE *)"Down", 0 };

static struct IntuiText g_menu_info_text = { 0, 1, JAM2, 6, 1, 0, (UBYTE *)"Info", 0 };
static struct IntuiText g_info_ok_text = { 0, 1, JAM2, 16, 1, 0, (UBYTE *)"OK", 0 };

static struct MenuItem g_help_info_item = {
    0, 0, 0, 70, 10, ITEMTEXT | ITEMENABLED | HIGHCOMP, 0,
    (APTR)&g_menu_info_text, 0, 0, 0, MENUNULL
};

static struct Menu g_help_menu = {
    0, 0, 0, 18, 10, MENUENABLED, (CONST_STRPTR)"?", &g_help_info_item,
    0, 0, 0, 0
};

static struct Gadget g_info_ok_gad = {
    0, 249, 100, 42, 14, GFLG_GADGHCOMP, GACT_RELVERIFY,
    GTYP_BOOLGADGET, 0, 0, &g_info_ok_text, 0, 0, GID_INFO_OK, 0
};

static struct Gadget g_scroll_down_gad = {
    0, 500, 74, 50, 14, GFLG_GADGHCOMP, GACT_RELVERIFY,
    GTYP_BOOLGADGET, 0, 0, &g_scroll_down_text, 0, 0, GID_SCROLL_DOWN, 0
};
static struct Gadget g_scroll_up_gad = {
    &g_scroll_down_gad, 500, 58, 50, 14, GFLG_GADGHCOMP, GACT_RELVERIFY,
    GTYP_BOOLGADGET, 0, 0, &g_scroll_up_text, 0, 0, GID_SCROLL_UP, 0
};
static struct Gadget g_track_gad = {
    &g_scroll_up_gad, 254, MODE_GADGET_Y, 72, 14, GFLG_GADGHCOMP, GACT_RELVERIFY,
    GTYP_BOOLGADGET, 0, 0, &g_track_text, 0, 0, GID_TRACK, 0
};
static struct Gadget g_releases_gad = {
    &g_track_gad, 158, MODE_GADGET_Y, 88, 14, GFLG_GADGHCOMP, GACT_RELVERIFY,
    GTYP_BOOLGADGET, 0, 0, &g_releases_text, 0, 0, GID_RELEASES, 0
};
static struct Gadget g_artist_gad = {
    &g_releases_gad, 72, MODE_GADGET_Y, 78, 14, GFLG_GADGHCOMP, GACT_RELVERIFY,
    GTYP_BOOLGADGET, 0, 0, &g_artist_text, 0, 0, GID_ARTIST, 0
};
static struct Gadget g_search_gad = {
    &g_artist_gad, 332, QUERY_GADGET_Y, 76, 14, GFLG_GADGHCOMP, GACT_RELVERIFY,
    GTYP_BOOLGADGET, 0, 0, &g_search_text, 0, 0, GID_SEARCH, 0
};
static struct Gadget g_query_gad = {
    &g_search_gad, 58, QUERY_GADGET_Y, 266, 14,
    GFLG_GADGHCOMP, GACT_RELVERIFY | GACT_STRINGLEFT,
    GTYP_STRGADGET, 0, 0, 0, 0, (APTR)&g_query_si, GID_QUERY, 0
};

static struct NewWindow g_new_window = {
    20, 20, 560, 180,
    0, 1,
    IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE |
    IDCMP_GADGETUP | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | IDCMP_MENUPICK,
    WFLG_CLOSEGADGET | WFLG_DEPTHGADGET | WFLG_SIZEGADGET |
    WFLG_DRAGBAR | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
    &g_query_gad,
    0,
    (STRPTR)APP_TITLE,
    0,
    0,
    430, 130, 1000, 700,
    WBENCHSCREEN
};


static struct Amitcp13BsdSockAddrIn g_addr;
static struct Amitcp13BsdFdSet g_rfds;
static struct Amitcp13BsdFdSet g_wfds;
static struct Amitcp13BsdTimeVal g_tv;
static ULONG g_wait_sigs;
static LONG g_one;
static int g_so_error;
static int g_so_error_len;

static UWORD text_len(const char *s)
{
    UWORD n = 0;
    while (s && s[n])
        ++n;
    return n;
}

static void copy_text(char *dst, ULONG dst_size, const char *src)
{
    ULONG i = 0;
    if (!dst || dst_size == 0)
        return;
    while (src && src[i] && i + 1 < dst_size) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_text(char *dst, ULONG dst_size, const char *src)
{
    ULONG i = 0;
    ULONG j = 0;
    while (dst && dst[i])
        ++i;
    while (dst && src && src[j] && i + 1 < dst_size)
        dst[i++] = src[j++];
    if (dst)
        dst[i] = 0;
}

static void append_char(char *dst, ULONG dst_size, char ch)
{
    ULONG i = 0;
    while (dst && dst[i])
        ++i;
    if (dst && i + 1 < dst_size) {
        dst[i++] = ch;
        dst[i] = 0;
    }
}


static void append_dec(char *dst, ULONG dst_size, LONG value)
{
    char tmp[16];
    ULONG v;
    UWORD n = 0;
    UWORD i;

    if (value < 0) {
        append_char(dst, dst_size, '-');
        v = (ULONG)(-value);
    } else {
        v = (ULONG)value;
    }
    do {
        tmp[n++] = (char)('0' + (v % 10UL));
        v /= 10UL;
    } while (v && n < sizeof(tmp));
    for (i = 0; i < n; ++i)
        append_char(dst, dst_size, tmp[n - 1 - i]);
}

static int key_match_at(const char *p, const char *key)
{
    while (*key) {
        if (*p++ != *key++)
            return 0;
    }
    return *p == '=';
}

static LONG parse_long_value(const char *p)
{
    LONG value = 0;
    int neg = 0;

    if (*p == '-') {
        neg = 1;
        ++p;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10L + (LONG)(*p - '0');
        ++p;
    }
    return neg ? -value : value;
}

static LONG config_get_long(const char *key, LONG fallback)
{
    char *p = g_config_buf;

    while (*p) {
        if (key_match_at(p, key))
            return parse_long_value(p + text_len(key) + 1);
        while (*p && *p != '\n' && *p != '\r')
            ++p;
        while (*p == '\n' || *p == '\r')
            ++p;
    }
    return fallback;
}

static void validate_window_def(struct NewWindow *nw)
{
    struct Screen *screen;
    WORD max_w;
    WORD max_h;

    max_w = nw->MaxWidth;
    max_h = nw->MaxHeight;
    screen = IntuitionBase ? IntuitionBase->ActiveScreen : 0;
    if (!screen && IntuitionBase)
        screen = IntuitionBase->FirstScreen;
    if (screen) {
        max_w = screen->Width;
        max_h = screen->Height;
        if (max_w > nw->MaxWidth)
            max_w = nw->MaxWidth;
        if (max_h > nw->MaxHeight)
            max_h = nw->MaxHeight;
    }

    if (nw->Width < nw->MinWidth)
        nw->Width = nw->MinWidth;
    if (nw->Height < nw->MinHeight)
        nw->Height = nw->MinHeight;
    if (nw->Width > max_w)
        nw->Width = max_w;
    if (nw->Height > max_h)
        nw->Height = max_h;
    if (nw->LeftEdge < 0)
        nw->LeftEdge = 0;
    if (nw->TopEdge < 0)
        nw->TopEdge = 0;
    if (nw->LeftEdge + nw->Width > max_w)
        nw->LeftEdge = (WORD)(max_w - nw->Width);
    if (nw->TopEdge + nw->Height > max_h)
        nw->TopEdge = (WORD)(max_h - nw->Height);
    if (nw->LeftEdge < 0)
        nw->LeftEdge = 0;
    if (nw->TopEdge < 0)
        nw->TopEdge = 0;
}

static void reset_default_window_def(void)
{
    g_new_window.LeftEdge = 20;
    g_new_window.TopEdge = 20;
    g_new_window.Width = 560;
    g_new_window.Height = 180;
    validate_window_def(&g_new_window);
}

static void load_config(void)
{
    BPTR f;
    LONG got;

    g_config_buf[0] = 0;
    f = Open((STRPTR)CONFIG_FILE, MODE_OLDFILE);
    if (!f)
        return;
    got = Read(f, g_config_buf, sizeof(g_config_buf) - 1);
    Close(f);
    if (got <= 0)
        return;
    g_config_buf[got] = 0;

    g_new_window.LeftEdge = (WORD)config_get_long("main_x", g_new_window.LeftEdge);
    g_new_window.TopEdge = (WORD)config_get_long("main_y", g_new_window.TopEdge);
    g_new_window.Width = (WORD)config_get_long("main_w", g_new_window.Width);
    g_new_window.Height = (WORD)config_get_long("main_h", g_new_window.Height);
    validate_window_def(&g_new_window);

}

static void save_config(void)
{
    BPTR f;
    char out[256];

    out[0] = 0;
    append_text(out, sizeof(out), "main_x=");
    append_dec(out, sizeof(out), g_new_window.LeftEdge);
    append_text(out, sizeof(out), "\nmain_y=");
    append_dec(out, sizeof(out), g_new_window.TopEdge);
    append_text(out, sizeof(out), "\nmain_w=");
    append_dec(out, sizeof(out), g_new_window.Width);
    append_text(out, sizeof(out), "\nmain_h=");
    append_dec(out, sizeof(out), g_new_window.Height);
    append_text(out, sizeof(out), "\n");

    f = Open((STRPTR)CONFIG_FILE, MODE_NEWFILE);
    if (!f)
        return;
    Write(f, out, text_len(out));
    Close(f);
}

static void remember_main_window(void)
{
    if (!g_win)
        return;
    g_new_window.LeftEdge = g_win->LeftEdge;
    g_new_window.TopEdge = g_win->TopEdge;
    g_new_window.Width = g_win->Width;
    g_new_window.Height = g_win->Height;
    validate_window_def(&g_new_window);
    save_config();
}

static void clear_results(void)
{
    UWORD i;
    g_result_count = 0;
    g_main_top = 0;
    g_results_truncated = 0;
    for (i = 0; i < RESULT_LINES; ++i) {
        g_results[i][0] = 0;
        g_result_values[i][0] = 0;
        g_result_select[i][0] = 0;
        g_result_keys[i][0] = 0;
    }
}

static void add_result_full(const char *line, const char *value, const char *select_text, const char *key)
{
    if (g_result_count >= RESULT_LINES)
        return;
    copy_text(g_results[g_result_count], RESULT_LINE_SIZE, line);
    copy_text(g_result_values[g_result_count], QUERY_SIZE, value ? value : line);
    copy_text(g_result_select[g_result_count], QUERY_SIZE, select_text ? select_text : line);
    copy_text(g_result_keys[g_result_count], sizeof(g_result_keys[g_result_count]), key ? key : "");
    ++g_result_count;
}

static void add_result(const char *line)
{
    add_result_full(line, line, line, "");
}

static int text_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static void add_or_replace_result_full(const char *line, const char *value, const char *select_text, const char *key)
{
    UWORD i;

    for (i = 0; i < g_result_count; ++i) {
        if (text_equal(g_results[i], line)) {
            copy_text(g_result_values[i], QUERY_SIZE, value ? value : line);
            copy_text(g_result_select[i], QUERY_SIZE, select_text ? select_text : line);
            copy_text(g_result_keys[i], sizeof(g_result_keys[i]), key ? key : "");
            return;
        }
    }
    add_result_full(line, value, select_text, key);
}

static void date_to_year(const char *date, char *year, ULONG year_size)
{
    ULONG i;

    if (!year || year_size == 0)
        return;
    year[0] = 0;
    if (!date || !date[0])
        return;
    for (i = 0; i < 4 && i + 1 < year_size; ++i) {
        if (date[i] < '0' || date[i] > '9')
            break;
        year[i] = date[i];
    }
    year[i] = 0;
    if (!year[0])
        copy_text(year, year_size, date);
}

static int text_less(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a < *b)
            return 1;
        if (*a > *b)
            return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b != 0;
}

static void swap_results(UWORD a, UWORD b)
{
    char tmp_line[RESULT_LINE_SIZE];
    char tmp_value[QUERY_SIZE];
    char tmp_select[QUERY_SIZE];
    char tmp_key[20];

    copy_text(tmp_line, sizeof(tmp_line), g_results[a]);
    copy_text(tmp_value, sizeof(tmp_value), g_result_values[a]);
    copy_text(tmp_select, sizeof(tmp_select), g_result_select[a]);
    copy_text(tmp_key, sizeof(tmp_key), g_result_keys[a]);
    copy_text(g_results[a], RESULT_LINE_SIZE, g_results[b]);
    copy_text(g_result_values[a], QUERY_SIZE, g_result_values[b]);
    copy_text(g_result_select[a], QUERY_SIZE, g_result_select[b]);
    copy_text(g_result_keys[a], sizeof(g_result_keys[a]), g_result_keys[b]);
    copy_text(g_results[b], RESULT_LINE_SIZE, tmp_line);
    copy_text(g_result_values[b], QUERY_SIZE, tmp_value);
    copy_text(g_result_select[b], QUERY_SIZE, tmp_select);
    copy_text(g_result_keys[b], sizeof(g_result_keys[b]), tmp_key);
}

static void sort_results_by_key(void)
{
    UWORD i;
    UWORD j;

    for (i = 0; i < g_result_count; ++i) {
        for (j = (UWORD)(i + 1); j < g_result_count; ++j) {
            if (g_result_keys[j][0] && (!g_result_keys[i][0] || text_less(g_result_keys[j], g_result_keys[i])))
                swap_results(i, j);
        }
    }
}

static void set_status(const char *s)
{
    copy_text(g_status, sizeof(g_status), s);
}

static void gui_pens(void)
{
    SetAPen(g_win->RPort, 1);
    SetBPen(g_win->RPort, 0);
    SetDrMd(g_win->RPort, JAM1);
}

static void draw_box(WORD x, WORD y, WORD w, WORD h)
{
    Move(g_win->RPort, x, y);
    Draw(g_win->RPort, (WORD)(x + w - 1), y);
    Draw(g_win->RPort, (WORD)(x + w - 1), (WORD)(y + h - 1));
    Draw(g_win->RPort, x, (WORD)(y + h - 1));
    Draw(g_win->RPort, x, y);
}

static void flash_rect(WORD x1, WORD y1, WORD x2, WORD y2)
{
    if (!g_win)
        return;
    SetDrMd(g_win->RPort, COMPLEMENT);
    RectFill(g_win->RPort, x1, y1, x2, y2);
    Delay(2);
    RectFill(g_win->RPort, x1, y1, x2, y2);
    gui_pens();
}

static void flash_gadget(struct Gadget *gad)
{
    if (!gad || gad->GadgetID == GID_QUERY)
        return;
    flash_rect(gad->LeftEdge, gad->TopEdge,
        (WORD)(gad->LeftEdge + gad->Width - 1),
        (WORD)(gad->TopEdge + gad->Height - 1));
}

static void flash_result_index(UWORD idx)
{
    UWORD row;
    WORD y;

    if (!g_win || idx < g_main_top)
        return;
    row = (UWORD)(idx - g_main_top);
    if (row >= main_visible_rows())
        return;
    y = (WORD)(RESULT_TEXT_Y - 8 + row * RESULT_ROW_H);
    flash_rect(10, y, (WORD)(g_win->Width - 18), (WORD)(y + RESULT_ROW_H - 1));
}

static void draw_mode_marks(void)
{
    const char *mark;

    SetAPen(g_win->RPort, 1);
    mark = (g_mode == MODE_ARTIST) ? "*" : " ";
    Move(g_win->RPort, 78, MODE_MARK_Y);
    Text(g_win->RPort, (STRPTR)mark, 1);
    mark = (g_mode == MODE_RELEASES) ? "*" : " ";
    Move(g_win->RPort, 164, MODE_MARK_Y);
    Text(g_win->RPort, (STRPTR)mark, 1);
    mark = (g_mode == MODE_TRACK) ? "*" : " ";
    Move(g_win->RPort, 260, MODE_MARK_Y);
    Text(g_win->RPort, (STRPTR)mark, 1);
}

static void draw_button_box(struct Window *win, struct Gadget *gad)
{
    if (!win || !gad)
        return;
    SetAPen(win->RPort, 1);
    Move(win->RPort, gad->LeftEdge, gad->TopEdge);
    Draw(win->RPort, (WORD)(gad->LeftEdge + gad->Width - 1), gad->TopEdge);
    Draw(win->RPort, (WORD)(gad->LeftEdge + gad->Width - 1), (WORD)(gad->TopEdge + gad->Height - 1));
    Draw(win->RPort, gad->LeftEdge, (WORD)(gad->TopEdge + gad->Height - 1));
    Draw(win->RPort, gad->LeftEdge, gad->TopEdge);
}

static void draw_info_dialog(struct Window *win)
{
    if (!win)
        return;
    SetBPen(win->RPort, 1);
    SetDrMd(win->RPort, JAM1);
    SetAPen(win->RPort, 1);
    RectFill(win->RPort, 0, 10, (WORD)(win->Width - 1), (WORD)(win->Height - 1));
    SetAPen(win->RPort, 0);
    Move(win->RPort, 14, 25);
    Text(win->RPort, (STRPTR)"AMusicBrainz for Kick1.3", text_len("AMusicBrainz for Kick1.3"));
    Move(win->RPort, 14, 39);
    Text(win->RPort, (STRPTR)"Version: v1.0", text_len("Version: v1.0"));
    Move(win->RPort, 14, 53);
    Text(win->RPort, (STRPTR)"by Marcel Jaehne", text_len("by Marcel Jaehne"));
    Move(win->RPort, 14, 67);
    Text(win->RPort, (STRPTR)"(c) 2026", text_len("(c) 2026"));
    Move(win->RPort, 14, 81);
    Text(win->RPort, (STRPTR)"If you want to buy me a coffe, send me a buck to:",
        text_len("If you want to buy me a coffe, send me a buck to:"));
    Move(win->RPort, 14, 95);
    Text(win->RPort, (STRPTR)"https://paypal.me/mytubefree",
        text_len("https://paypal.me/mytubefree"));
    draw_button_box(win, &g_info_ok_gad);
    RefreshGList(&g_info_ok_gad, win, 0, 1);
}

static void open_info_dialog(void)
{
    struct Window *iw;
    struct NewWindow nw;
    struct IntuiMessage *msg;
    ULONG cls;
    APTR addr;
    int done;

    nw.LeftEdge = 45;
    nw.TopEdge = 40;
    nw.Width = 540;
    nw.Height = 130;
    nw.DetailPen = 0;
    nw.BlockPen = 1;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_GADGETUP;
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_ACTIVATE | WFLG_SMART_REFRESH;
    nw.FirstGadget = &g_info_ok_gad;
    nw.CheckMark = 0;
    nw.Title = (STRPTR)"AMusicBrainz Info";
    nw.Screen = 0;
    nw.BitMap = 0;
    nw.MinWidth = 540;
    nw.MinHeight = 130;
    nw.MaxWidth = 540;
    nw.MaxHeight = 130;
    nw.Type = WBENCHSCREEN;

    iw = OpenWindow(&nw);
    if (!iw) {
        set_status("Info window failed");
        redraw();
        return;
    }
    draw_info_dialog(iw);
    done = 0;
    while (!done) {
        WaitPort(iw->UserPort);
        while ((msg = (struct IntuiMessage *)GetMsg(iw->UserPort)) != 0) {
            cls = msg->Class;
            addr = msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                done = 1;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(iw);
                draw_info_dialog(iw);
                EndRefresh(iw, TRUE);
            } else if (cls == IDCMP_GADGETUP && addr) {
                if (((struct Gadget *)addr)->GadgetID == GID_INFO_OK)
                    done = 1;
            }
        }
    }
    CloseWindow(iw);
    redraw();
}

static void relayout_gadgets(void)
{
    WORD w;
    WORD query_w;

    if (!g_win)
        return;
    w = g_win->Width;
    query_w = (WORD)(w - 294);
    if (query_w < 150)
        query_w = 150;
    g_query_gad.Width = query_w;
    g_search_gad.LeftEdge = (WORD)(g_query_gad.LeftEdge + query_w + 8);
    g_scroll_up_gad.LeftEdge = (WORD)(w - 118);
    g_scroll_down_gad.LeftEdge = (WORD)(w - 62);
    if (g_scroll_up_gad.LeftEdge < 250)
        g_scroll_up_gad.LeftEdge = 250;
    if (g_scroll_down_gad.LeftEdge < 306)
        g_scroll_down_gad.LeftEdge = 306;
    g_scroll_up_gad.TopEdge = (WORD)(g_win->Height - 22);
    g_scroll_down_gad.TopEdge = (WORD)(g_win->Height - 22);
}

static void redraw(void)
{
    WORD w;
    WORD h;
    WORD y;
    WORD box_h;
    UWORD i;
    UWORD visible;

    if (!g_win)
        return;
    relayout_gadgets();
    w = g_win->Width;
    h = g_win->Height;
    gui_pens();
    SetAPen(g_win->RPort, 0);
    RectFill(g_win->RPort, 0, 10, (WORD)(w - 1), (WORD)(h - 1));
    SetAPen(g_win->RPort, 1);
    draw_box(3, 10, (WORD)(w - 6), (WORD)(h - 21));
    Move(g_win->RPort, 12, QUERY_LABEL_Y);
    Text(g_win->RPort, (STRPTR)"Query", 5);
    RefreshGList(&g_query_gad, g_win, 0, 5);

    Move(g_win->RPort, 8, SEPARATOR_Y);
    Draw(g_win->RPort, (WORD)(w - 9), SEPARATOR_Y);
    Move(g_win->RPort, 8, (WORD)(SEPARATOR_Y + 1));
    Draw(g_win->RPort, (WORD)(w - 9), (WORD)(SEPARATOR_Y + 1));

    box_h = (WORD)(h - RESULT_BOTTOM_PAD);
    if (box_h < 24)
        box_h = 24;
    draw_box(8, RESULT_BOX_Y, (WORD)(w - 16), box_h);
    y = RESULT_TEXT_Y;
    visible = (UWORD)((box_h - 16) / RESULT_ROW_H);
    if (g_main_top >= g_result_count)
        g_main_top = 0;
    for (i = 0; i < visible && (UWORD)(g_main_top + i) < g_result_count; ++i) {
        Move(g_win->RPort, 14, y);
        Text(g_win->RPort, (STRPTR)g_results[g_main_top + i], text_len(g_results[g_main_top + i]));
        y = (WORD)(y + RESULT_ROW_H);
    }
    RefreshGList(&g_scroll_up_gad, g_win, 0, 2);
    Move(g_win->RPort, 8, (WORD)(h - 14));
    Text(g_win->RPort, (STRPTR)g_status, text_len(g_status));
    draw_mode_marks();
}



static void url_encode_query(const char *src, char *dst, ULONG dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    ULONG i = 0;
    ULONG j = 0;
    UBYTE c;

    while (src && src[i] && j + 1 < dst_size) {
        c = (UBYTE)src[i++];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            dst[j++] = (char)c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else if (j + 3 < dst_size) {
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0x0f];
            dst[j++] = hex[c & 0x0f];
        }
    }
    dst[j] = 0;
}

static int call_socket(struct Library *base, int domain, int type, int protocol)
{
    register int d0 __asm("d0") = domain;
    register int d1 __asm("d1") = type;
    register int d2 __asm("d2") = protocol;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-30:W)" : "+r"(d0), "+r"(d1), "+r"(d2) : "r"(a6) : "a0", "a1", "cc", "memory");
    return d0;
}

static int call_connect(struct Library *base, int fd, const struct Amitcp13BsdSockAddr *addr, int addrlen)
{
    register int d0 __asm("d0") = fd;
    register const struct Amitcp13BsdSockAddr *a0 __asm("a0") = addr;
    register int d1 __asm("d1") = addrlen;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-54:W)" : "+r"(d0), "+r"(a0), "+r"(d1) : "r"(a6) : "a1", "cc", "memory");
    return d0;
}

static int call_send(struct Library *base, int fd, const void *buf, int len)
{
    register int d0 __asm("d0") = fd;
    register const void *a0 __asm("a0") = buf;
    register int d1 __asm("d1") = len;
    register int d2 __asm("d2") = 0;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-66:W)" : "+r"(d0), "+r"(a0), "+r"(d1), "+r"(d2) : "r"(a6) : "a1", "cc", "memory");
    return d0;
}

static int call_recv(struct Library *base, int fd, void *buf, int len)
{
    register int d0 __asm("d0") = fd;
    register void *a0 __asm("a0") = buf;
    register int d1 __asm("d1") = len;
    register int d2 __asm("d2") = 0;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-78:W)" : "+r"(d0), "+r"(a0), "+r"(d1), "+r"(d2) : "r"(a6) : "a1", "cc", "memory");
    return d0;
}

static int call_ioctl(struct Library *base, int fd, ULONG request, void *argp)
{
    register int d0 __asm("d0") = fd;
    register ULONG d1 __asm("d1") = request;
    register void *a0 __asm("a0") = argp;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-114:W)" : "+r"(d0), "+r"(d1), "+r"(a0) : "r"(a6) : "a1", "cc", "memory");
    return d0;
}

static int call_close_socket(struct Library *base, int fd)
{
    register int d0 __asm("d0") = fd;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-120:W)" : "+r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static int call_waitselect(struct Library *base, int nfds, struct Amitcp13BsdFdSet *readfds,
                           struct Amitcp13BsdFdSet *writefds, const struct Amitcp13BsdTimeVal *timeout)
{
    register int d0 __asm("d0") = nfds;
    register ULONG *d1 __asm("d1") = &g_wait_sigs;
    register struct Amitcp13BsdFdSet *a0 __asm("a0") = readfds;
    register struct Amitcp13BsdFdSet *a1 __asm("a1") = writefds;
    register struct Amitcp13BsdFdSet *a2 __asm("a2") = 0;
    register const struct Amitcp13BsdTimeVal *a3 __asm("a3") = timeout;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-126:W)" : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1), "+r"(a2), "+r"(a3) : "r"(a6) : "cc", "memory");
    return d0;
}

static int call_errno(struct Library *base)
{
    register int d0 __asm("d0");
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-162:W)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static struct hostent *call_gethostbyname(struct Library *base, const char *name)
{
    register const char *a0 __asm("a0") = name;
    register struct Library *a6 __asm("a6") = base;
    register struct hostent *d0 __asm("d0");
    __asm volatile ("jsr a6@(-210:W)" : "=r"(d0), "+r"(a0) : "r"(a6) : "d1", "a1", "cc", "memory");
    return d0;
}

static int call_getsockopt(struct Library *base, int fd, int level, int optname, void *optval, int *optlen)
{
    register int d0 __asm("d0") = fd;
    register int d1 __asm("d1") = level;
    register int d2 __asm("d2") = optname;
    register void *a0 __asm("a0") = optval;
    register int *a1 __asm("a1") = optlen;
    register struct Library *a6 __asm("a6") = base;
    __asm volatile ("jsr a6@(-96:W)" : "+r"(d0), "+r"(d1), "+r"(d2), "+r"(a0), "+r"(a1) : "r"(a6) : "cc", "memory");
    return d0;
}

static int wait_fd(int fd, int write, LONG sec)
{
    AMITCP13_BSD_FD_ZERO(&g_rfds);
    AMITCP13_BSD_FD_ZERO(&g_wfds);
    if (write)
        AMITCP13_BSD_FD_SET(fd, &g_wfds);
    else
        AMITCP13_BSD_FD_SET(fd, &g_rfds);
    g_tv.tv_sec = sec;
    g_tv.tv_usec = 0;
    g_wait_sigs = 0;
    return call_waitselect(g_sock_base, fd + 1, write ? 0 : &g_rfds, write ? &g_wfds : 0, &g_tv);
}

static int connect_musicbrainz(void)
{
    struct hostent *he;
    int fd;
    int r;
    int err;

    if (!g_sock_base) {
        g_sock_base = OpenLibrary((STRPTR)"bsdsocket.library", 0);
        if (!g_sock_base)
            return -1;
    }
    he = call_gethostbyname(g_sock_base, MB_HOST);
    if (!he || !he->h_addr_list || !he->h_addr_list[0])
        return -2;
    fd = call_socket(g_sock_base, AMITCP13_AF_INET, AMITCP13_SOCK_STREAM, AMITCP13_IPPROTO_TCP);
    if (fd < 0)
        return -3;
    g_one = 1;
    call_ioctl(g_sock_base, fd, AMITCP13_FIONBIO, &g_one);
    g_addr.sin_len = sizeof(g_addr);
    g_addr.sin_family = AMITCP13_AF_INET;
    g_addr.sin_port = MB_PORT;
    g_addr.sin_addr.s_addr = *(ULONG *)he->h_addr_list[0];
    r = call_connect(g_sock_base, fd, (const struct Amitcp13BsdSockAddr *)&g_addr, sizeof(g_addr));
    if (r < 0) {
        err = call_errno(g_sock_base);
        if (err != AMITCP13_EINPROGRESS && err != AMITCP13_EALREADY) {
            call_close_socket(g_sock_base, fd);
            return -4;
        }
        r = wait_fd(fd, 1, 15);
        if (r <= 0 || !AMITCP13_BSD_FD_ISSET(fd, &g_wfds)) {
            call_close_socket(g_sock_base, fd);
            return -5;
        }
        g_so_error = -1;
        g_so_error_len = sizeof(g_so_error);
        r = call_getsockopt(g_sock_base, fd, AMITCP13_SOL_SOCKET, AMITCP13_SO_ERROR,
            &g_so_error, &g_so_error_len);
        if (r < 0 || g_so_error != 0) {
            call_close_socket(g_sock_base, fd);
            return -6;
        }
    }
    return fd;
}

static void build_request_path(char *req, ULONG req_size, const char *path)
{
    req[0] = 0;
    append_text(req, req_size, "GET ");
    append_text(req, req_size, path);
    append_text(req, req_size, " HTTP/1.0\r\n");
    append_text(req, req_size, "Host: musicbrainz.org\r\n");
    append_text(req, req_size, "User-Agent: AMusicBrainz/0.1 (amiga; bsdsocket.library)\r\n");
    append_text(req, req_size, "Connection: close\r\n\r\n");
}

static void build_and_query(const char *src, const char *field, char *out, ULONG out_size)
{
    char token[64];
    char enc[96];
    ULONG n;
    int first = 1;

    out[0] = 0;
    while (src && *src) {
        while (*src == ' ' || *src == '\t')
            ++src;
        if (!*src)
            break;
        n = 0;
        while (*src && *src != ' ' && *src != '\t' && n + 1 < sizeof(token))
            token[n++] = *src++;
        token[n] = 0;
        while (*src && *src != ' ' && *src != '\t')
            ++src;
        if (!token[0])
            continue;
        url_encode_query(token, enc, sizeof(enc));
        if (!first)
            append_text(out, out_size, "%20AND%20");
        append_text(out, out_size, field);
        append_char(out, out_size, ':');
        append_text(out, out_size, enc);
        first = 0;
    }
    if (first) {
        append_text(out, out_size, field);
        append_char(out, out_size, ':');
    }
}

static void build_search_path(char *path, ULONG path_size)
{
    char query[192];
    const char *entity;
    const char *field;

    if (g_mode == MODE_ARTIST) {
        entity = "artist";
        field = "artist";
    } else if (g_mode == MODE_RELEASES) {
        entity = "release";
        field = "release";
    } else {
        entity = "recording";
        field = "recording";
    }
    build_and_query(g_query, field, query, sizeof(query));
    path[0] = 0;
    append_text(path, path_size, "/ws/2/");
    append_text(path, path_size, entity);
    append_text(path, path_size, "?query=");
    append_text(path, path_size, query);
    append_text(path, path_size, "&fmt=json&limit=20");
}

static int http_fetch_path(const char *path)
{
    char req[HTTP_REQ_SIZE];
    ULONG used = 0;
    int fd;
    int r;
    int idle = 0;

    fd = connect_musicbrainz();
    if (fd < 0)
        return fd;
    build_request_path(req, sizeof(req), path);
    r = call_send(g_sock_base, fd, req, text_len(req));
    if (r < 0) {
        call_close_socket(g_sock_base, fd);
        return -7;
    }
    while (used + 1 < HTTP_BUF_SIZE && idle < 20) {
        r = wait_fd(fd, 0, 1);
        if (r <= 0) {
            ++idle;
            continue;
        }
        if (!AMITCP13_BSD_FD_ISSET(fd, &g_rfds)) {
            ++idle;
            continue;
        }
        r = call_recv(g_sock_base, fd, g_http_buf + used, (int)(HTTP_BUF_SIZE - used - 1));
        if (r > 0) {
            used += (ULONG)r;
            idle = 0;
        } else if (r == 0) {
            break;
        } else {
            int err = call_errno(g_sock_base);
            if (err == AMITCP13_EWOULDBLOCK || err == AMITCP13_EAGAIN || err == AMITCP13_EINPROGRESS) {
                ++idle;
                continue;
            }
            call_close_socket(g_sock_base, fd);
            return -8;
        }
    }
    g_http_buf[used] = 0;
    call_close_socket(g_sock_base, fd);
    return (int)used;
}

static int http_fetch(void)
{
    char path[HTTP_REQ_SIZE];

    build_search_path(path, sizeof(path));
    return http_fetch_path(path);
}

static char *find_field(char *start, const char *field)
{
    char pat[40];
    ULONG i = 0;
    char *p;

    pat[i++] = '"';
    while (field && *field && i + 4 < sizeof(pat))
        pat[i++] = *field++;
    pat[i++] = '"';
    pat[i++] = ':';
    pat[i] = 0;
    for (p = start; p && *p; ++p) {
        char *a = p;
        char *b = pat;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (!*b)
            return a;
    }
    return 0;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static void extract_string(char *start, const char *field, char *out, ULONG out_size)
{
    char *p = find_field(start, field);
    ULONG i = 0;

    out[0] = 0;
    if (!p)
        return;
    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p != '"')
        return;
    ++p;
    while (*p && *p != '"' && i + 1 < out_size) {
        UBYTE c = (UBYTE)*p;
        if (c == '\\' && p[1]) {
            ++p;
            if (*p == 'u' && hex_value(p[1]) >= 0 && hex_value(p[2]) >= 0 &&
                hex_value(p[3]) >= 0 && hex_value(p[4]) >= 0) {
                ULONG code = ((ULONG)hex_value(p[1]) << 12) |
                             ((ULONG)hex_value(p[2]) << 8) |
                             ((ULONG)hex_value(p[3]) << 4) |
                             (ULONG)hex_value(p[4]);
                if (code >= 32 && code <= 255)
                    out[i++] = (char)code;
                else if (code >= 32)
                    out[i++] = '?';
                p += 5;
                continue;
            }
            if (*p == 'n' || *p == 'r' || *p == 't') {
                ++p;
                continue;
            }
            c = (UBYTE)*p++;
            if (c >= 32)
                out[i++] = (char)c;
            continue;
        }
        if (c == 0xc2 && (UBYTE)p[1] >= 0x80) {
            out[i++] = p[1];
            p += 2;
            continue;
        }
        if (c == 0xc3 && (UBYTE)p[1] >= 0x80) {
            out[i++] = (char)((UBYTE)p[1] + 0x40);
            p += 2;
            continue;
        }
        if (c < 0x80) {
            if (c >= 32)
                out[i++] = (char)c;
            ++p;
            continue;
        }
        out[i++] = '?';
        ++p;
    }
    out[i] = 0;
}



static char *object_end(char *p)
{
    int depth = 0;
    int in_str = 0;

    while (p && *p) {
        if (*p == '"' && (p == g_http_buf || p[-1] != '\\'))
            in_str = !in_str;
        if (!in_str) {
            if (*p == '{')
                ++depth;
            else if (*p == '}') {
                --depth;
                if (depth == 0)
                    return p;
            }
        }
        ++p;
    }
    return 0;
}

static char *find_array_start(char *start, const char *field)
{
    char *p;

    p = find_field(start, field);
    if (!p)
        return 0;
    while (*p && *p != '[')
        ++p;
    if (*p != '[')
        return 0;
    return p + 1;
}

static char *next_array_object(char *p)
{
    int in_str = 0;

    while (p && *p) {
        if (*p == '"' && (p == g_http_buf || p[-1] != '\\'))
            in_str = !in_str;
        if (!in_str) {
            if (*p == ']')
                return 0;
            if (*p == '{')
                return p;
        }
        ++p;
    }
    return 0;
}

static void parse_results(void)
{
    char *p = g_http_buf;
    char *end;
    const char *array_name;
    char id[QUERY_SIZE];
    char name[64];
    char title[64];
    char date[20];
    char country[12];
    char line[RESULT_LINE_SIZE];

    clear_results();
    while (*p && !(*p == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n'))
        ++p;
    if (*p)
        p += 4;

    array_name = (g_mode == MODE_ARTIST) ? "artists" : ((g_mode == MODE_RELEASES) ? "releases" : "recordings");
    p = find_array_start(p, array_name);
    if (!p) {
        add_result("No results");
        return;
    }

    while ((p = next_array_object(p)) != 0 && g_result_count < SEARCH_RESULT_LIMIT) {
        end = object_end(p);
        if (!end)
            break;
        *end = 0;
        id[0] = name[0] = title[0] = date[0] = country[0] = 0;
        extract_string(p, "id", id, sizeof(id));
        extract_string(p, "name", name, sizeof(name));
        extract_string(p, "title", title, sizeof(title));
        extract_string(p, "first-release-date", date, sizeof(date));
        if (!date[0])
            extract_string(p, "date", date, sizeof(date));
        extract_string(p, "country", country, sizeof(country));

        line[0] = 0;
        if (g_mode == MODE_ARTIST && name[0]) {
            append_text(line, sizeof(line), name);
            if (country[0]) {
                append_text(line, sizeof(line), " / ");
                append_text(line, sizeof(line), country);
            }
            add_result_full(line, id, name, "");
        } else if (g_mode != MODE_ARTIST && title[0]) {
            append_text(line, sizeof(line), title);
            if (date[0]) {
                append_text(line, sizeof(line), " (");
                append_text(line, sizeof(line), date);
                append_text(line, sizeof(line), ")");
            }
            if (name[0]) {
                append_text(line, sizeof(line), " - ");
                append_text(line, sizeof(line), name);
            }
            add_result_full(line, id, title, date[0] ? date : "9999");
        }
        *end = '}';
        p = end + 1;
    }
    if (g_result_count >= SEARCH_RESULT_LIMIT && p && next_array_object(p))
        g_results_truncated = 1;
    if (g_result_count == 0)
        add_result("No results");
}

static void make_artist_album_path(char *path, ULONG path_size, const char *artist_id)
{
    path[0] = 0;
    append_text(path, path_size, "/ws/2/release-group?artist=");
    append_text(path, path_size, artist_id);
    append_text(path, path_size, "&type=album&fmt=json&limit=100");
}

static void make_release_group_release_path(char *path, ULONG path_size, const char *release_group_id)
{
    path[0] = 0;
    append_text(path, path_size, "/ws/2/release?release-group=");
    append_text(path, path_size, release_group_id);
    append_text(path, path_size, "&fmt=json&limit=1");
}

static void make_album_track_path(char *path, ULONG path_size, const char *release_id)
{
    path[0] = 0;
    append_text(path, path_size, "/ws/2/release/");
    append_text(path, path_size, release_id);
    append_text(path, path_size, "?inc=recordings&fmt=json");
}

static ULONG parse_uword_text(const char *s)
{
    ULONG value = 0;

    while (s && *s) {
        if (*s >= '0' && *s <= '9')
            value = value * 10UL + (ULONG)(*s - '0');
        ++s;
    }
    return value;
}

static void parse_album_results(void)
{
    char *p = g_http_buf;
    char *end;
    char id[QUERY_SIZE];
    char title[64];
    char date[20];
    char year[8];
    char line[RESULT_LINE_SIZE];
    char value[QUERY_SIZE];

    clear_results();
    while (*p && !(*p == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n'))
        ++p;
    if (*p)
        p += 4;
    p = find_array_start(p, "release-groups");
    if (!p) {
        add_result("No albums found");
        return;
    }

    while ((p = next_array_object(p)) != 0 && g_result_count < RESULT_LINES) {
        end = object_end(p);
        if (!end)
            break;
        *end = 0;
        id[0] = title[0] = date[0] = year[0] = value[0] = 0;
        extract_string(p, "id", id, sizeof(id));
        extract_string(p, "title", title, sizeof(title));
        extract_string(p, "first-release-date", date, sizeof(date));
        if (!date[0])
            extract_string(p, "date", date, sizeof(date));
        date_to_year(date, year, sizeof(year));
        if (id[0] && title[0]) {
            line[0] = 0;
            if (year[0]) {
                append_text(line, sizeof(line), year);
                append_text(line, sizeof(line), " - ");
            } else {
                append_text(line, sizeof(line), "???? - ");
            }
            append_text(line, sizeof(line), title);
            append_text(value, sizeof(value), "rg:");
            append_text(value, sizeof(value), id);
            add_or_replace_result_full(line, value, title, year[0] ? year : "9999");
        }
        *end = '}';
        p = end + 1;
    }
    if (g_result_count >= RESULT_LINES && p && next_array_object(p))
        g_results_truncated = 1;
    sort_results_by_key();
    if (g_result_count == 0)
        add_result("No albums found");
}

static void parse_track_results(void)
{
    char *p = g_http_buf;
    char *end;
    char number[20];
    char title[64];
    char line[RESULT_LINE_SIZE];
    ULONG pos;
    char key[20];

    clear_results();
    while (*p && !(*p == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n'))
        ++p;
    if (*p)
        p += 4;
    p = find_array_start(p, "tracks");
    if (!p) {
        add_result("No tracks found");
        return;
    }

    while ((p = next_array_object(p)) != 0 && g_result_count < RESULT_LINES) {
        end = object_end(p);
        if (!end)
            break;
        *end = 0;
        number[0] = title[0] = 0;
        extract_string(p, "number", number, sizeof(number));
        extract_string(p, "title", title, sizeof(title));
        if (number[0] && title[0]) {
            pos = parse_uword_text(number);
            key[0] = (char)('0' + ((pos / 100UL) % 10UL));
            key[1] = (char)('0' + ((pos / 10UL) % 10UL));
            key[2] = (char)('0' + (pos % 10UL));
            key[3] = 0;
            line[0] = 0;
            append_text(line, sizeof(line), number);
            append_text(line, sizeof(line), ". ");
            append_text(line, sizeof(line), title);
            add_result_full(line, "", title, key);
        }
        *end = '}';
        p = end + 1;
    }
    if (g_result_count >= RESULT_LINES && p && next_array_object(p))
        g_results_truncated = 1;
    sort_results_by_key();
    if (g_result_count == 0)
        add_result("No tracks found");
}

static void fetch_artist_albums(const char *artist_id, const char *artist_name)
{
    char path[HTTP_REQ_SIZE];
    int r;

    copy_text(g_query, sizeof(g_query), artist_name);
    set_status("Loading albums...");
    redraw();
    make_artist_album_path(path, sizeof(path), artist_id);
    r = http_fetch_path(path);
    if (r <= 0) {
        clear_results();
        add_result("Album request failed");
        set_status("Album API failed");
        redraw();
        return;
    }
    parse_album_results();
    g_hit_kind = HIT_ALBUMS;
    if (g_results_truncated)
        set_status("Album list truncated");
    else
        set_status("Albums loaded");
    redraw();
}

static int parse_first_release_id(char *out, ULONG out_size)
{
    char *p = g_http_buf;
    char *end;

    out[0] = 0;
    while (*p && !(*p == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n'))
        ++p;
    if (*p)
        p += 4;
    p = find_array_start(p, "releases");
    if (!p)
        return 0;
    p = next_array_object(p);
    if (!p)
        return 0;
    end = object_end(p);
    if (!end)
        return 0;
    *end = 0;
    extract_string(p, "id", out, out_size);
    *end = '}';
    return out[0] != 0;
}


static int is_release_group_ref(const char *id)
{
    return id && id[0] == 'r' && id[1] == 'g' && id[2] == ':';
}

static void fetch_album_tracks(const char *release_id, const char *release_title)
{
    char path[HTTP_REQ_SIZE];
    char actual_id[QUERY_SIZE];
    char status[96];
    int r;

    set_status("Loading tracks...");
    redraw();
    if (is_release_group_ref(release_id)) {
        set_status("Resolving release...");
        redraw();
        make_release_group_release_path(path, sizeof(path), release_id + 3);
        r = http_fetch_path(path);
        if (r <= 0 || !parse_first_release_id(actual_id, sizeof(actual_id))) {
            clear_results();
            add_result("Release lookup failed");
            set_status("Release API failed");
            redraw();
            return;
        }
        release_id = actual_id;
    }
    make_album_track_path(path, sizeof(path), release_id);
    r = http_fetch_path(path);
    if (r <= 0) {
        clear_results();
        add_result("Track request failed");
        set_status("Track API failed");
        redraw();
        return;
    }
    parse_track_results();
    g_hit_kind = HIT_TRACKS;
    if (g_results_truncated)
        set_status("Too many results - refine search");
    else {
        status[0] = 0;
        append_text(status, sizeof(status), "Release: ");
        append_text(status, sizeof(status), release_title);
        set_status(status);
    }
    redraw();
}



static UWORD main_visible_rows(void)
{
    WORD box_h;

    if (!g_win)
        return 1;
    box_h = (WORD)(g_win->Height - RESULT_BOTTOM_PAD);
    if (box_h < 24)
        box_h = 24;
    return (UWORD)((box_h - 16) / RESULT_ROW_H);
}

static void scroll_results_up(void)
{
    if (g_main_top > 0)
        --g_main_top;
    redraw();
}

static void scroll_results_down(void)
{
    UWORD visible;

    visible = main_visible_rows();
    if ((UWORD)(g_main_top + visible) < g_result_count)
        ++g_main_top;
    redraw();
}

static int main_result_index_at_y(WORD mouse_y, UWORD *out_idx)
{
    UWORD visible;
    WORD row;

    if (!g_win || !out_idx)
        return 0;
    visible = main_visible_rows();
    if (mouse_y < RESULT_BOX_Y || mouse_y >= (WORD)(RESULT_TEXT_Y + visible * RESULT_ROW_H))
        return 0;
    row = (WORD)((mouse_y - RESULT_TEXT_Y) / RESULT_ROW_H);
    if (row < 0)
        row = 0;
    if ((UWORD)row >= visible || (UWORD)(g_main_top + (UWORD)row) >= g_result_count)
        return 0;
    *out_idx = (UWORD)(g_main_top + (UWORD)row);
    return 1;
}

static int handle_main_result_click(WORD mouse_y)
{
    UWORD idx;

    if (!main_result_index_at_y(mouse_y, &idx))
        return 0;
    if (!g_result_values[idx][0])
        return 0;
    if (g_hit_kind == HIT_ARTISTS) {
        flash_result_index(idx);
        fetch_artist_albums(g_result_values[idx], g_result_select[idx]);
        return 1;
    }
    if (g_hit_kind == HIT_ALBUMS) {
        flash_result_index(idx);
        fetch_album_tracks(g_result_values[idx], g_result_select[idx]);
        return 1;
    }
    return 0;
}

static void handle_menu(UWORD code)
{
    struct MenuItem *item;
    UWORD menu;
    UWORD item_no;

    while (code != MENUNULL) {
        item = ItemAddress(&g_help_menu, code);
        menu = MENUNUM(code);
        item_no = ITEMNUM(code);
        if (menu == 0 && item_no == 0)
            open_info_dialog();
        code = item ? item->NextSelect : MENUNULL;
    }
}

static void do_search(void)
{
    int r;

    if (!g_query[0]) {
        set_status("Enter search text");
        redraw();
        return;
    }
    set_status("Searching...");
    clear_results();
    redraw();
    r = http_fetch();
    if (r <= 0) {
        clear_results();
        add_result("Request failed");
        set_status("Network/API failed");
        redraw();
        return;
    }
    parse_results();
    g_hit_kind = (g_mode == MODE_ARTIST) ? HIT_ARTISTS : ((g_mode == MODE_RELEASES) ? HIT_ALBUMS : HIT_TRACKS);
    if (g_results_truncated)
        set_status("Too many results - refine search");
    else
        set_status("Done");
    redraw();
}

static int open_libs(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 0);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 0);
    return IntuitionBase && GfxBase;
}

static void close_libs(void)
{
    if (g_sock_base)
        CloseLibrary(g_sock_base);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
}

int main(void)
{
    struct IntuiMessage *msg;
    ULONG cls;
    UWORD code;
    WORD mouse_y;
    struct Gadget *gad;
    int running = 1;

    if (!open_libs())
        return 20;
    g_http_buf = (char *)AllocMem(HTTP_BUF_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    if (!g_http_buf) {
        close_libs();
        return 20;
    }
    load_config();
    clear_results();
    add_result("Enter a query and press Search");
    g_win = OpenWindow(&g_new_window);
    if (!g_win) {
        reset_default_window_def();
        g_win = OpenWindow(&g_new_window);
    }
    if (!g_win) {
        FreeMem(g_http_buf, HTTP_BUF_SIZE);
        close_libs();
        return 20;
    }
    redraw();
    SetMenuStrip(g_win, &g_help_menu);

    while (running) {
        Wait(1UL << g_win->UserPort->mp_SigBit);
        while ((msg = (struct IntuiMessage *)GetMsg(g_win->UserPort)) != 0) {
            cls = msg->Class;
            code = msg->Code;
            mouse_y = msg->MouseY;
            gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                running = 0;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(g_win);
                redraw();
                EndRefresh(g_win, TRUE);
            } else if (cls == IDCMP_NEWSIZE) {
                redraw();
            } else if (cls == IDCMP_MENUPICK) {
                handle_menu(code);
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                handle_main_result_click(mouse_y);
            } else if (cls == IDCMP_GADGETUP && gad) {
                flash_gadget(gad);
                if (gad->GadgetID == GID_SEARCH || gad->GadgetID == GID_QUERY)
                    do_search();
                else if (gad->GadgetID == GID_ARTIST) {
                    g_mode = MODE_ARTIST;
                    set_status("Mode: Artist");
                    redraw();
                } else if (gad->GadgetID == GID_RELEASES) {
                    g_mode = MODE_RELEASES;
                    set_status("Mode: Releases");
                    redraw();
                } else if (gad->GadgetID == GID_TRACK) {
                    g_mode = MODE_TRACK;
                    set_status("Mode: Track");
                    redraw();
                } else if (gad->GadgetID == GID_SCROLL_UP) {
                    scroll_results_up();
                } else if (gad->GadgetID == GID_SCROLL_DOWN) {
                    scroll_results_down();
                }
            } else if (cls == IDCMP_VANILLAKEY && code == 13) {
                do_search();
            } else if (cls == IDCMP_VANILLAKEY && (code == 'u' || code == 'U')) {
                scroll_results_up();
            } else if (cls == IDCMP_VANILLAKEY && (code == 'd' || code == 'D')) {
                scroll_results_down();
            }
        }
    }

    remember_main_window();
    ClearMenuStrip(g_win);
    CloseWindow(g_win);
    FreeMem(g_http_buf, HTTP_BUF_SIZE);
    close_libs();
    return 0;
}
