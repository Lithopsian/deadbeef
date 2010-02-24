/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009-2010 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include "../../deadbeef.h"
#include <gtk/gtk.h>
#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif
#if HAVE_NOTIFY
#include <libnotify/notify.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include "gtkui.h"
#include "ddblistview.h"
#include "mainplaylist.h"
#include "search.h"
#include "progress.h"
#include "interface.h"
#include "callbacks.h"
#include "support.h"
#include "parser.h"
#include "drawing.h"
#include "trkproperties.h"

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

static DB_gui_t plugin;
DB_functions_t *deadbeef;

static intptr_t gtk_tid;

// main widgets
GtkWidget *mainwin;
GtkWidget *searchwin;
GtkStatusIcon *trayicon;
GtkWidget *traymenu;

// orange on dark color scheme
float colo_dark_orange[COLO_COUNT][3] = {
    { 0x7f/255.f, 0x7f/255.f, 0x7f/255.f }, // cursor
    { 0x1d/255.f, 0x1f/255.f, 0x1b/255.f }, // odd
    { 0x21/255.f, 0x23/255.f, 0x1f/255.f }, // even
    { 0xaf/255.f, 0xa7/255.f, 0x9e/255.f }, // sel odd
    { 0xa7/255.f, 0x9f/255.f, 0x96/255.f }, // sel even
    { 0xf4/255.f, 0x7e/255.f, 0x46/255.f }, // text
    { 0,          0,          0          }, // sel text
    { 0x1d/255.f, 0x1f/255.f, 0x1b/255.f }, // seekbar back
    { 0xf4/255.f, 0x7e/255.f, 0x46/255.f }, // seekbar front
    { 0x1d/255.f, 0x1f/255.f, 0x1b/255.f }, // volumebar back
    { 0xf4/255.f, 0x7e/255.f, 0x46/255.f }, // volumebar front
    { 0xf4/255.f, 0x7e/255.f, 0x46/255.f }, // dragdrop marker
};

float colo_white_blue[COLO_COUNT][3] = {
    { 0x7f/255.f, 0x7f/255.f, 0x7f/255.f }, // cursor
    { 1,          1,          1          }, // odd
    { 0xea/255.f, 0xeb/255.f, 0xec/255.f }, // even
    { 0x24/255.f, 0x89/255.f, 0xb8/255.f }, // sel odd
    { 0x20/255.f, 0x85/255.f, 0xb4/255.f }, // sel even
    { 0,          0,          0          }, // text
    { 1,          1,          1          }, // sel text
    { 0x1d/255.f, 0x1f/255.f, 0x1b/255.f }, // seekbar back
    { 0x24/255.f, 0x89/255.f, 0xb8/255.f }, // seekbar front
    { 0x1d/255.f, 0x1f/255.f, 0x1b/255.f }, // volumebar back
    { 0x24/255.f, 0x89/255.f, 0xb8/255.f }, // volumebar front
    { 0x09/255.f, 0x22/255.f, 0x3a/255.f }, // dragdrop marker
};

// current color scheme
float colo_current[COLO_COUNT][3];

void
theme_set_cairo_source_rgb (cairo_t *cr, int col) {
    cairo_set_source_rgb (cr, colo_current[col][0], colo_current[col][1], colo_current[col][2]);
}

void
theme_set_fg_color (int col) {
    draw_set_fg_color (colo_current[col]);
}

void
theme_set_bg_color (int col) {
    draw_set_bg_color (colo_current[col]);
}

// that must be called before gtk_init
GtkWidget *theme_treeview;
void
gtkpl_init (void) {
    //memcpy (colo_current, colo_system_gtk, sizeof (colo_current));
    //memcpy (colo_current, colo_dark_orange, sizeof (colo_current));
    memcpy (colo_current, colo_white_blue, sizeof (colo_current));
    theme_treeview = gtk_tree_view_new ();
    GTK_WIDGET_UNSET_FLAGS (theme_treeview, GTK_CAN_FOCUS);
    gtk_widget_show (theme_treeview);
    GtkWidget *vbox1 = lookup_widget (mainwin, "vbox1");
    gtk_box_pack_start (GTK_BOX (vbox1), theme_treeview, FALSE, FALSE, 0);
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (theme_treeview), TRUE);
}

void
gtkpl_free (DdbListview *pl) {
#if 0
    if (colhdr_anim.timeline) {
        timeline_free (colhdr_anim.timeline, 1);
        colhdr_anim.timeline = 0;
    }
#endif
//    g_object_unref (theme_treeview);
}



struct fromto_t {
    int from;
    int to;
};
static gboolean
update_win_title_idle (gpointer data);
static gboolean
redraw_seekbar_cb (gpointer nothing);

// update status bar and window title
static int sb_context_id = -1;
static char sb_text[512];
static float last_songpos = -1;
static char sbitrate[20] = "";
static struct timeval last_br_update;

static gboolean
update_songinfo (gpointer ctx) {
    int iconified = gdk_window_get_state(mainwin->window) & GDK_WINDOW_STATE_ICONIFIED;
    if (!GTK_WIDGET_VISIBLE (mainwin) || iconified) {
        return FALSE;
    }
    char sbtext_new[512] = "-";
    float songpos = last_songpos;

    float pl_totaltime = deadbeef->pl_get_totaltime ();
    int daystotal = (int)pl_totaltime / (3600*24);
    int hourtotal = ((int)pl_totaltime / 3600) % 24;
    int mintotal = ((int)pl_totaltime/60) % 60;
    int sectotal = ((int)pl_totaltime) % 60;

    char totaltime_str[512] = "";
    if (daystotal == 0)
        snprintf (totaltime_str, sizeof (totaltime_str), "%d:%02d:%02d", hourtotal, mintotal, sectotal);

    else if (daystotal == 1)
        snprintf (totaltime_str, sizeof (totaltime_str), "1 day %d:%02d:%02d", hourtotal, mintotal, sectotal);

    else
        snprintf (totaltime_str, sizeof (totaltime_str), "%d days %d:%02d:%02d", daystotal, hourtotal, mintotal, sectotal);



    DB_playItem_t *track = deadbeef->streamer_get_playing_track ();
    DB_fileinfo_t *c = deadbeef->streamer_get_current_fileinfo ();

    float duration = track ? deadbeef->pl_get_item_duration (track) : -1;

    if (deadbeef->get_output ()->state () == OUTPUT_STATE_STOPPED || !track || !c) {
        snprintf (sbtext_new, sizeof (sbtext_new), "Stopped | %d tracks | %s total playtime", deadbeef->pl_getcount (PL_MAIN), totaltime_str);
        songpos = 0;
    }
    else {
        float playpos = deadbeef->streamer_get_playpos ();
        int minpos = playpos / 60;
        int secpos = playpos - minpos * 60;
        int mindur = duration / 60;
        int secdur = duration - mindur * 60;

        const char *mode = c->channels == 1 ? "Mono" : "Stereo";
        int samplerate = c->samplerate;
        int bitspersample = c->bps;
        songpos = playpos;
        //        codec_unlock ();

        char t[100];
        if (duration >= 0) {
            snprintf (t, sizeof (t), "%d:%02d", mindur, secdur);
        }
        else {
            strcpy (t, "-:--");
        }

        struct timeval tm;
        gettimeofday (&tm, NULL);
        if (tm.tv_sec - last_br_update.tv_sec + (tm.tv_usec - last_br_update.tv_usec) / 1000000.0 >= 0.3) {
            memcpy (&last_br_update, &tm, sizeof (tm));
            int bitrate = deadbeef->streamer_get_apx_bitrate ();
            if (bitrate > 0) {
                snprintf (sbitrate, sizeof (sbitrate), "| %4d kbps ", bitrate);
            }
            else {
                sbitrate[0] = 0;
            }
        }
        const char *spaused = deadbeef->get_output ()->state () == OUTPUT_STATE_PAUSED ? "Paused | " : "";
        snprintf (sbtext_new, sizeof (sbtext_new), "%s%s %s| %dHz | %d bit | %s | %d:%02d / %s | %d tracks | %s total playtime", spaused, track->filetype ? track->filetype:"-", sbitrate, samplerate, bitspersample, mode, minpos, secpos, t, deadbeef->pl_getcount (PL_MAIN), totaltime_str);
    }

    if (strcmp (sbtext_new, sb_text)) {
        strcpy (sb_text, sbtext_new);

        // form statusline
        // FIXME: don't update if window is not visible
        GtkStatusbar *sb = GTK_STATUSBAR (lookup_widget (mainwin, "statusbar"));
        if (sb_context_id == -1) {
            sb_context_id = gtk_statusbar_get_context_id (sb, "msg");
        }

        gtk_statusbar_pop (sb, sb_context_id);
        gtk_statusbar_push (sb, sb_context_id, sb_text);
    }

    void seekbar_draw (GtkWidget *widget);
    void seekbar_expose (GtkWidget *widget, int x, int y, int w, int h);
    if (mainwin) {
        GtkWidget *widget = lookup_widget (mainwin, "seekbar");
        // translate volume to seekbar pixels
        songpos /= duration;
        songpos *= widget->allocation.width;
        if (fabs (songpos - last_songpos) > 0.01) {
            seekbar_draw (widget);
            seekbar_expose (widget, 0, 0, widget->allocation.width, widget->allocation.height);
            last_songpos = songpos;
        }
    }
    return FALSE;
}

gboolean
on_trayicon_scroll_event               (GtkWidget       *widget,
                                        GdkEventScroll  *event,
                                        gpointer         user_data)
{
    float vol = deadbeef->volume_get_db ();
    if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_RIGHT) {
        vol += 1;
    }
    else if (event->direction == GDK_SCROLL_DOWN || event->direction == GDK_SCROLL_LEFT) {
        vol -= 1;
    }
    if (vol > 0) {
        vol = 0;
    }
    else if (vol < -60) {
        vol = -60;
    }
    deadbeef->volume_set_db (vol);
    GtkWidget *volumebar = lookup_widget (mainwin, "volumebar");
    gdk_window_invalidate_rect (volumebar->window, NULL, FALSE);
    return FALSE;
}

void
mainwin_toggle_visible (void) {
    int iconified = gdk_window_get_state(mainwin->window) & GDK_WINDOW_STATE_ICONIFIED;
    if (GTK_WIDGET_VISIBLE (mainwin) && !iconified) {
        gtk_widget_hide (mainwin);
    }
    else {
        int x = deadbeef->conf_get_int ("mainwin.geometry.x", 40);
        int y = deadbeef->conf_get_int ("mainwin.geometry.y", 40);
        int w = deadbeef->conf_get_int ("mainwin.geometry.w", 500);
        int h = deadbeef->conf_get_int ("mainwin.geometry.h", 300);
        gtk_window_move (GTK_WINDOW (mainwin), x, y);
        gtk_window_resize (GTK_WINDOW (mainwin), w, h);
        if (deadbeef->conf_get_int ("mainwin.geometry.maximized", 0)) {
            gtk_window_maximize (GTK_WINDOW (mainwin));
        }
        if (iconified) {
            gtk_window_deiconify (GTK_WINDOW(mainwin));
        }
        else {
            gtk_window_present (GTK_WINDOW (mainwin));
        }
    }
}

#if GTK_MINOR_VERSION<=14

gboolean
on_trayicon_activate (GtkWidget       *widget,
                                        GdkEvent  *event,
                                        gpointer         user_data)
{
    mainwin_toggle_visible ();
    return FALSE;
}

#else

gboolean
on_trayicon_button_press_event (GtkWidget       *widget,
                                        GdkEventButton  *event,
                                        gpointer         user_data)
{
    if (event->button == 1) {
        mainwin_toggle_visible ();
    }
    else if (event->button == 2) {
        deadbeef->sendmessage (M_PAUSESONG, 0, 0, 0);
    }
    return FALSE;
}
#endif

gboolean
on_trayicon_popup_menu (GtkWidget       *widget,
        guint button,
        guint time,
                                        gpointer         user_data)
{
    gtk_menu_popup (GTK_MENU (traymenu), NULL, NULL, gtk_status_icon_position_menu, trayicon, button, time);
    return FALSE;
}

static gboolean
activate_cb (gpointer nothing) {
    gtk_widget_show (mainwin);
    gtk_window_present (GTK_WINDOW (mainwin));
    return FALSE;
}

static int
gtkui_on_activate (DB_event_t *ev, uintptr_t data) {
    g_idle_add (activate_cb, NULL);
    return 0;
}

void
redraw_queued_tracks (DdbListview *pl, int list) {
#if 0 // FIXME: port
    DdbListviewIter it = deadbeef->pl_get_for_idx_and_iter (pl->scrollpos, list);
    int i = ddb_listview_get_vscroll_pos (pl);
    while (it && i < pl->scrollpos + pl->nvisiblerows) {
        if (deadbeef->pl_playqueue_test (it) != -1) {
            ddb_listview_draw_row (pl, i, it);
        }
        DdbListviewIter next = deadbeef->pl_get_next (it, list);
        deadbeef->pl_item_unref (it);
        it = next;
        i++;
    }
    if (it) {
        deadbeef->pl_item_unref (it);
    }
#endif
}

static gboolean
redraw_queued_tracks_cb (gpointer nothing) {
    redraw_queued_tracks (DDB_LISTVIEW (lookup_widget (mainwin, "playlist")), PL_MAIN);
    redraw_queued_tracks (DDB_LISTVIEW (lookup_widget (searchwin, "searchlist")), PL_SEARCH);
    return FALSE;
}

void
gtkpl_songchanged_wrapper (int from, int to) {
    struct fromto_t *ft = malloc (sizeof (struct fromto_t));
    ft->from = from;
    ft->to = to;
    g_idle_add (update_win_title_idle, ft);
    if (ft->to == -1) {
        // redraw seekbar
        g_idle_add (redraw_seekbar_cb, NULL);
    }
    g_idle_add (redraw_queued_tracks_cb, NULL);
}

static int
gtkui_on_songchanged (DB_event_trackchange_t *ev, uintptr_t data) {
    gtkpl_songchanged_wrapper (ev->from, ev->to);
    return 0;
}

void
set_tray_tooltip (const char *text) {
#if (GTK_MINOR_VERSION < 16)
        gtk_status_icon_set_tooltip (trayicon, text);
#else
        gtk_status_icon_set_tooltip_text (trayicon, text);
#endif
}

struct trackinfo_t {
    int index;
    DB_playItem_t *track;
};

static void
current_track_changed (DB_playItem_t *it) {
    char str[600];
    if (it) {
        char dname[512];
        deadbeef->pl_format_item_display_name (it, dname, 512);
        snprintf (str, sizeof (str), "DeaDBeeF - %s", dname);
    }
    else {
        strcpy (str, "DeaDBeeF");
    }
    gtk_window_set_title (GTK_WINDOW (mainwin), str);
    set_tray_tooltip (str);
}

static gboolean
trackinfochanged_cb (gpointer data) {
    struct trackinfo_t *ti = (struct trackinfo_t *)data;
    GtkWidget *playlist = lookup_widget (mainwin, "playlist");
//    gtkpl_redraw_pl_row (&main_playlist, ti->index, ti->track);
    ddb_listview_draw_row (DDB_LISTVIEW (playlist), ti->index, (DdbListviewIter)ti->track);
    if (ti->track == deadbeef->pl_getcurrent ()) {
        current_track_changed (ti->track);
    }
    free (ti);
    return FALSE;
}

static int
gtkui_on_trackinfochanged (DB_event_track_t *ev, uintptr_t data) {
    struct trackinfo_t *ti = malloc (sizeof (struct trackinfo_t));
    ti->index = ev->index;
    ti->track = ev->track;
    g_idle_add (trackinfochanged_cb, ti);
    return 0;
}

static gboolean
paused_cb (gpointer nothing) {
    DB_playItem_t *curr = deadbeef->pl_getcurrent ();
    if (curr) {
        int idx = deadbeef->pl_get_idx_of (curr);
        GtkWidget *playlist = lookup_widget (mainwin, "playlist");
        //gtkpl_redraw_pl_row (&main_playlist, idx, curr);
        ddb_listview_draw_row (DDB_LISTVIEW (playlist), idx, (DdbListviewIter)curr);
    }
    return FALSE;
}

static int
gtkui_on_paused (DB_event_state_t *ev, uintptr_t data) {
    g_idle_add (paused_cb, NULL);
}

void
playlist_refresh (void) {
    DdbListview *ps = DDB_LISTVIEW (lookup_widget (mainwin, "playlist"));
    ddb_listview_refresh (ps, DDB_REFRESH_LIST | DDB_REFRESH_VSCROLL | DDB_EXPOSE_LIST);
    search_refresh ();
}

static gboolean
playlistchanged_cb (gpointer none) {
    playlist_refresh ();
    return FALSE;
}

static int
gtkui_on_playlistchanged (DB_event_t *ev, uintptr_t data) {
    g_idle_add (playlistchanged_cb, NULL);
}

static gboolean
playlistswitch_cb (gpointer none) {
    GtkWidget *tabstrip = lookup_widget (mainwin, "tabstrip");
    gdk_window_invalidate_rect (tabstrip->window, NULL, FALSE);
    playlist_refresh ();
    search_refresh ();
    return FALSE;
}

static int
gtkui_on_playlistswitch (DB_event_t *ev, uintptr_t data) {
    g_idle_add (playlistswitch_cb, NULL);
}

static int
gtkui_on_frameupdate (DB_event_t *ev, uintptr_t data) {
    g_idle_add (update_songinfo, NULL);
}

static int
gtkui_on_volumechanged (DB_event_t *ev, uintptr_t data) {
    GtkWidget *volumebar = lookup_widget (mainwin, "volumebar");
    gdk_window_invalidate_rect (volumebar->window, NULL, FALSE);
    return 0;
}

static int
gtkui_on_configchanged (DB_event_t *ev, uintptr_t data) {
    // order and looping
    const char *w;

    // order
    const char *orderwidgets[3] = { "order_linear", "order_shuffle", "order_random" };
    w = orderwidgets[deadbeef->conf_get_int ("playback.order", PLAYBACK_ORDER_LINEAR)];
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (lookup_widget (mainwin, w)), TRUE);

    // looping
    const char *loopingwidgets[3] = { "loop_all", "loop_disable", "loop_single" };
    w = loopingwidgets[deadbeef->conf_get_int ("playback.loop", PLAYBACK_MODE_LOOP_ALL)];
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (lookup_widget (mainwin, w)), TRUE);

    // scroll follows playback
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (lookup_widget (mainwin, "scroll_follows_playback")), deadbeef->conf_get_int ("playlist.scroll.followplayback", 0) ? TRUE : FALSE);

    // cursor follows playback
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (lookup_widget (mainwin, "cursor_follows_playback")), deadbeef->conf_get_int ("playlist.scroll.cursorfollowplayback", 0) ? TRUE : FALSE);

    // stop after current
    int stop_after_current = deadbeef->conf_get_int ("playlist.stop_after_current", 0);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (lookup_widget (mainwin, "stop_after_current")), stop_after_current ? TRUE : FALSE);
    return 0;
}

static gboolean
outputchanged_cb (gpointer nothing) {
    preferences_fill_soundcards ();
    return FALSE;
}

static int
gtkui_on_outputchanged (DB_event_t *ev, uintptr_t nothing) {
    g_idle_add (outputchanged_cb, NULL);
    return 0;
}

void
on_playlist_load_activate              (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new ("Load Playlist", GTK_WINDOW (mainwin), GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);

    GtkFileFilter* flt;
    flt = gtk_file_filter_new ();
    gtk_file_filter_set_name (flt, "DeaDBeeF playlist files (*.dbpl)");
    gtk_file_filter_add_pattern (flt, "*.dbpl");
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dlg), flt);

    if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_OK)
    {
        gchar *fname = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dlg));
        gtk_widget_destroy (dlg);
        if (fname) {
            /*int res = */deadbeef->pl_load (fname);
            g_free (fname);
            main_refresh ();
            search_refresh ();
        }
    }
    else {
        gtk_widget_destroy (dlg);
    }
}
void
on_add_audio_cd_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    deadbeef->pl_add_file ("all.cda", NULL, NULL);
    playlist_refresh ();
}

void
on_add_location_activate               (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    GtkWidget *dlg= create_addlocationdlg ();
    int res = gtk_dialog_run (GTK_DIALOG (dlg));
    if (res == GTK_RESPONSE_OK) {
        GtkEntry *entry = GTK_ENTRY (lookup_widget (dlg, "addlocation_entry"));
        if (entry) {
            const char *text = gtk_entry_get_text (entry);
            if (text) {
                deadbeef->pl_add_file (text, NULL, NULL);
                playlist_refresh ();
            }
        }
    }
    gtk_widget_destroy (dlg);
}
static int
search_get_count (void) {
    return deadbeef->pl_getcount (PL_SEARCH);
}

void
search_playlist_init (GtkWidget *widget) {
#if 0
    extern GtkWidget *searchwin;
    // init playlist control structure, and put it into widget user-data
    memset (&search_playlist, 0, sizeof (search_playlist));
    search_playlist.title = "search";
    search_playlist.playlist = widget;
    search_playlist.header = lookup_widget (searchwin, "searchheader");
    search_playlist.scrollbar = lookup_widget (searchwin, "searchscroll");
    search_playlist.hscrollbar = lookup_widget (searchwin, "searchhscroll");
    assert (search_playlist.header);
    assert (search_playlist.scrollbar);
//    main_playlist.pcurr = &search_current;
//    search_playlist.pcount = &search_count;
    search_playlist.get_count = search_get_count;
//    search_playlist.multisel = 0;
    search_playlist.iterator = PL_SEARCH;
    search_playlist.scrollpos = 0;
    search_playlist.hscrollpos = 0;
//    search_playlist.row = -1;
    search_playlist.clicktime = -1;
    search_playlist.nvisiblerows = 0;

    // create default set of columns
    DB_conf_item_t *col = deadbeef->conf_find ("search.column.", NULL);
    if (!col) {
        gtkpl_column_append (&search_playlist, gtkpl_column_alloc ("Artist / Album", 150, DB_COLUMN_ARTIST_ALBUM, NULL, 0));
        gtkpl_column_append (&search_playlist, gtkpl_column_alloc ("Track №", 50, DB_COLUMN_TRACK, NULL, 1));
        gtkpl_column_append (&search_playlist, gtkpl_column_alloc ("Title / Track Artist", 150, DB_COLUMN_TITLE, NULL, 0));
        gtkpl_column_append (&search_playlist, gtkpl_column_alloc ("Duration", 50, DB_COLUMN_DURATION, NULL, 0));
    }
    else {
        while (col) {
            gtkpl_append_column_from_textdef (&search_playlist, col->value);
            col = deadbeef->conf_find ("search.column.", col);
        }
    }
    gtk_object_set_data (GTK_OBJECT (search_playlist.playlist), "ps", &search_playlist);
    gtk_object_set_data (GTK_OBJECT (search_playlist.header), "ps", &search_playlist);
    gtk_object_set_data (GTK_OBJECT (search_playlist.scrollbar), "ps", &search_playlist);
    gtk_object_set_data (GTK_OBJECT (search_playlist.hscrollbar), "ps", &search_playlist);
#endif
}

static void
songchanged (DdbListview *ps, int from, int to) {
    if (!ddb_listview_is_scrolling (ps) && to != -1) {
        if (deadbeef->conf_get_int ("playlist.scroll.followplayback", 0)) {
            ddb_listview_scroll_to (ps, to);
        }
        if (deadbeef->conf_get_int ("playlist.scroll.cursorfollowplayback", 0)) {
            ddb_listview_set_cursor (ps, to);
        }
    }

    if (from >= 0) {
        DB_playItem_t *it = deadbeef->pl_get_for_idx_and_iter (from, PL_MAIN);
        ddb_listview_draw_row (ps, from, it);
        deadbeef->pl_item_unref (it);
    }
    if (to >= 0) {
        DB_playItem_t *it = deadbeef->pl_get_for_idx_and_iter (to, PL_MAIN);
        ddb_listview_draw_row (ps, to, it);
        deadbeef->pl_item_unref (it);
    }
}

#if HAVE_NOTIFY
static NotifyNotification* notification;
#endif

static gboolean
update_win_title_idle (gpointer data) {
    struct fromto_t *ft = (struct fromto_t *)data;
    int from = ft->from;
    int to = ft->to;
    free (ft);

    // show notification
#if HAVE_NOTIFY
    if (to != -1 && deadbeef->conf_get_int ("gtkui.notify.enable", 0)) {
        DB_playItem_t *track = deadbeef->pl_get_for_idx (to);
        if (track) {
            char cmd [1024];
            deadbeef->pl_format_title (track, -1, cmd, sizeof (cmd), -1, deadbeef->conf_get_str ("gtkui.notify.format", NOTIFY_DEFAULT_FORMAT));
            if (notify_is_initted ()) {
                if (notification) {
                    notify_notification_close (notification, NULL);
                }
                notification = notify_notification_new ("DeaDBeeF", cmd, NULL, NULL);
                if (notification) {
                    notify_notification_set_timeout (notification, NOTIFY_EXPIRES_DEFAULT);
                    notify_notification_show (notification, NULL);
                }
            }
            deadbeef->pl_item_unref (track);
        }
    }
#endif

    // update window title
    if (from >= 0 || to >= 0) {
        if (to >= 0) {
            DB_playItem_t *it = deadbeef->pl_get_for_idx (to);
            if (it) { // it might have been deleted after event was sent
                current_track_changed (it);
                deadbeef->pl_item_unref (it);
            }
        }
        else {
            gtk_window_set_title (GTK_WINDOW (mainwin), "DeaDBeeF");
            set_tray_tooltip ("DeaDBeeF");
        }
    }
    // update playlist view
    songchanged (DDB_LISTVIEW (lookup_widget (mainwin, "playlist")), from, to);
    return FALSE;
}

static gboolean
redraw_seekbar_cb (gpointer nothing) {
    void seekbar_draw (GtkWidget *widget);
    void seekbar_expose (GtkWidget *widget, int x, int y, int w, int h);
    GtkWidget *widget = lookup_widget (mainwin, "seekbar");
    seekbar_draw (widget);
    seekbar_expose (widget, 0, 0, widget->allocation.width, widget->allocation.height);
    return FALSE;
}

void
gtkui_thread (void *ctx) {
    // let's start some gtk
    g_thread_init (NULL);
    add_pixmap_directory (PREFIX "/share/deadbeef/pixmaps");
    gdk_threads_init ();
    gdk_threads_enter ();
    gtk_set_locale ();
#if HAVE_NOTIFY
    notify_init ("DeaDBeeF");
#endif

    int argc = 2;
    const char **argv = alloca (sizeof (char *) * argc);
    argv[0] = "deadbeef";
    argv[1] = "--sync";
    if (!deadbeef->conf_get_int ("gtkui.sync", 0)) {
        argc = 1;
    }
    gtk_init (&argc, (char ***)&argv);

    // system tray icon
    traymenu = create_traymenu ();
    GdkPixbuf *trayicon_pixbuf = create_pixbuf ("play_24.png");
    trayicon = gtk_status_icon_new_from_pixbuf (trayicon_pixbuf);
    set_tray_tooltip ("DeaDBeeF");
    //gtk_status_icon_set_title (GTK_STATUS_ICON (trayicon), "DeaDBeeF");
#if GTK_MINOR_VERSION <= 14
    g_signal_connect ((gpointer)trayicon, "activate", G_CALLBACK (on_trayicon_activate), NULL);
#else
    g_signal_connect ((gpointer)trayicon, "scroll_event", G_CALLBACK (on_trayicon_scroll_event), NULL);
    g_signal_connect ((gpointer)trayicon, "button_press_event", G_CALLBACK (on_trayicon_button_press_event), NULL);
#endif
    g_signal_connect ((gpointer)trayicon, "popup_menu", G_CALLBACK (on_trayicon_popup_menu), NULL);

    mainwin = create_mainwin ();
    gtkpl_init ();

    GdkPixbuf *mainwin_icon_pixbuf;
    mainwin_icon_pixbuf = create_pixbuf ("play_24.png");
    if (mainwin_icon_pixbuf)
    {
        gtk_window_set_icon (GTK_WINDOW (mainwin), mainwin_icon_pixbuf);
        gdk_pixbuf_unref (mainwin_icon_pixbuf);
    }
    {
        int x = deadbeef->conf_get_int ("mainwin.geometry.x", 40);
        int y = deadbeef->conf_get_int ("mainwin.geometry.y", 40);
        int w = deadbeef->conf_get_int ("mainwin.geometry.w", 500);
        int h = deadbeef->conf_get_int ("mainwin.geometry.h", 300);
        gtk_window_move (GTK_WINDOW (mainwin), x, y);
        gtk_window_resize (GTK_WINDOW (mainwin), w, h);
        if (deadbeef->conf_get_int ("mainwin.geometry.maximized", 0)) {
            gtk_window_maximize (GTK_WINDOW (mainwin));
        }
    }

    gtkui_on_configchanged (NULL, 0);

    // visibility of statusbar and headers
    GtkWidget *header_mi = lookup_widget (mainwin, "view_headers");
    GtkWidget *sb_mi = lookup_widget (mainwin, "view_status_bar");
    GtkWidget *header = lookup_widget (mainwin, "header");
    GtkWidget *sb = lookup_widget (mainwin, "statusbar");
    if (deadbeef->conf_get_int ("gtkui.headers.visible", 1)) {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (header_mi), TRUE);
    }
    else {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (header_mi), FALSE);
        gtk_widget_hide (header);
    }
    if (deadbeef->conf_get_int ("gtkui.statusbar.visible", 1)) {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (sb_mi), TRUE);
    }
    else {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (sb_mi), FALSE);
        gtk_widget_hide (sb);
    }

    searchwin = create_searchwin ();
    gtk_window_set_transient_for (GTK_WINDOW (searchwin), GTK_WINDOW (mainwin));
    main_playlist_init (lookup_widget (mainwin, "playlist"));
    search_playlist_init (lookup_widget (searchwin, "searchlist"));

    progress_init ();
    gtk_widget_show (mainwin);

    gtk_main ();
#if HAVE_NOTIFY
    notify_uninit ();
#endif
    gdk_threads_leave ();
}

static int
gtkui_start (void) {
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_ACTIVATE, DB_CALLBACK (gtkui_on_activate), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_SONGCHANGED, DB_CALLBACK (gtkui_on_songchanged), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_TRACKINFOCHANGED, DB_CALLBACK (gtkui_on_trackinfochanged), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_PAUSED, DB_CALLBACK (gtkui_on_paused), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_PLAYLISTCHANGED, DB_CALLBACK (gtkui_on_playlistchanged), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_FRAMEUPDATE, DB_CALLBACK (gtkui_on_frameupdate), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_VOLUMECHANGED, DB_CALLBACK (gtkui_on_volumechanged), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_CONFIGCHANGED, DB_CALLBACK (gtkui_on_configchanged), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_OUTPUTCHANGED, DB_CALLBACK (gtkui_on_outputchanged), 0);
    deadbeef->ev_subscribe (DB_PLUGIN (&plugin), DB_EV_PLAYLISTSWITCH, DB_CALLBACK (gtkui_on_playlistswitch), 0);
    // gtk must be running in separate thread
    gtk_tid = deadbeef->thread_start (gtkui_thread, NULL);

    return 0;
}

static gboolean
quit_gtk_cb (gpointer nothing) {
    gtk_main_quit ();
    return FALSE;
}

static int
gtkui_stop (void) {
    trace ("unsubscribing events\n");
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_ACTIVATE, DB_CALLBACK (gtkui_on_activate), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_SONGCHANGED, DB_CALLBACK (gtkui_on_songchanged), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_TRACKINFOCHANGED, DB_CALLBACK (gtkui_on_trackinfochanged), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_PAUSED, DB_CALLBACK (gtkui_on_paused), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_PLAYLISTCHANGED, DB_CALLBACK (gtkui_on_playlistchanged), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_FRAMEUPDATE, DB_CALLBACK (gtkui_on_frameupdate), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_VOLUMECHANGED, DB_CALLBACK (gtkui_on_volumechanged), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_CONFIGCHANGED, DB_CALLBACK (gtkui_on_configchanged), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_OUTPUTCHANGED, DB_CALLBACK (gtkui_on_outputchanged), 0);
    deadbeef->ev_unsubscribe (DB_PLUGIN (&plugin), DB_EV_PLAYLISTSWITCH, DB_CALLBACK (gtkui_on_playlistswitch), 0);
    trace ("quitting gtk\n");
    g_idle_add (quit_gtk_cb, NULL);
    trace ("waiting for gtk thread to finish\n");
    deadbeef->thread_join (gtk_tid);
    trace ("gtk thread finished\n");
    gtk_tid = 0;
    main_playlist_free ();
// FIXME: port
//    columns_free (&search_columns);
    trace ("gtkui_stop completed\n");
    return 0;
}

DB_plugin_t *
gtkui_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}

static const char settings_dlg[] =
    "property \"Run gtk_init with --sync (debug mode)\" checkbox gtkui.sync 0;\n"
#if HAVE_NOTIFY
    "property \"Enable OSD notifications\" checkbox gtkui.notify.enable 0;\n"
    "property \"Notification format\" entry gtkui.notify.format \"" NOTIFY_DEFAULT_FORMAT "\";\n"
#endif
;

// define plugin interface
static DB_gui_t plugin = {
    DB_PLUGIN_SET_API_VERSION
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.nostop = 1,
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.name = "Standard GTK2 user interface",
    .plugin.descr = "Default DeaDBeeF GUI",
    .plugin.author = "Alexey Yakovenko",
    .plugin.email = "waker@users.sourceforge.net",
    .plugin.website = "http://deadbeef.sf.net",
    .plugin.start = gtkui_start,
    .plugin.stop = gtkui_stop,
    .plugin.configdialog = settings_dlg,
};
