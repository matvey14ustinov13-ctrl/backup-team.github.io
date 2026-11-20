/* ============================================================================
 * zynx-gui - graphical front-end for zynx-install.
 *
 * C + GTK3, no Python and no terminal window.  Runs as the live desktop user;
 * the privileged engine (/usr/local/bin/zynx-install --auto ...) is started
 * through "sudo -n" (passwordless on the live medium) or pkexec.
 *
 * Engine progress: /run/zynx-install.status (line 1 percent, line 2 text).
 * Engine log:      /var/log/zynx-install.log
 * ==========================================================================*/

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define APP_TITLE "Zynx Linux"
#define ENGINE "/usr/local/bin/zynx-install"
#define LOG_FILE "/var/log/zynx-install.log"
#define STATUS_FILE "/run/zynx-install.status"
#define MIN_DISK_GIB 12

/* Allows running a test stub instead of the real engine. */
static const gchar *engine_path = ENGINE;
static const gchar *log_path = LOG_FILE;
static const gchar *status_path = STATUS_FILE;

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */
typedef struct {
    gchar *path;
    gchar *name;
    gchar *model;
    gchar *tran;
    guint64 size;
} Disk;

typedef struct {
    gchar *path;
    gchar *fstype;
    guint64 size;
} Part;

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *header;
    GtkWidget *back_btn;
    GtkWidget *next_btn;
    GtkWidget *reboot_btn;

    /* selection */
    gchar *disk_path;
    guint64 disk_size;
    int do_format;
    int set_default;
    int swap_gib;
    int root_gib;
    gchar *edition;      /* full | minimal | console */
    gchar *shell;        /* bash | zsh | fish */
    gchar *username;
    gchar *password;
    gchar *root_password;
    gchar *hostname;
    gchar *tz;
    gchar *locale;
    gchar *keyboard;

    /* widgets */
    GtkWidget *disk_list;
    GtkWidget *disk_scroll;
    GtkWidget *format_switch;
    GtkWidget *format_hint;
    GtkWidget *disk_note;
    GtkWidget *edition_list;
    GtkWidget *shell_combo;
    GtkWidget *user_entry;
    GtkWidget *pass_entry;
    GtkWidget *pass2_entry;
    GtkWidget *root_entry;
    GtkWidget *host_entry;
    GtkWidget *account_error;
    GtkWidget *tz_combo;
    GtkWidget *locale_combo;
    GtkWidget *keyboard_combo;
    GtkWidget *root_spin;
    GtkWidget *swap_combo;
    GtkWidget *default_check;
    GtkWidget *settings_error;
    GtkWidget *summary_grid;
    GtkWidget *summary_plan;
    GtkWidget *summary_note;
    GtkWidget *install_status;
    GtkWidget *install_hint;
    GtkWidget *progress;
    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;
    GtkWidget *log_expander;
    GtkWidget *done_icon;
    GtkWidget *done_title;
    GtkWidget *done_text;
    GtkWidget *confirm;
    GtkWidget *confirm_label;
    GtkWidget *confirm_entry;
    GtkWidget *confirm_error;
    GtkWidget *confirm_cancel;
    GtkWidget *confirm_ok;
    gchar *confirm_word;

    /* install run */
    GPid pid;
    gboolean running;
    gboolean failed;
    gboolean child_seen;
    gint child_code;
    gsize log_pos;
    guint poll_id;
} App;

static App *app_new(GtkApplication *gapp);
static void show_page(App *a, const gchar *name);
static void update_status(App *a);

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */
static gchar *run_capture(const gchar *const *argv)
{
    gchar *out = NULL;
    GError *err = NULL;
    if (!g_spawn_sync(NULL, (gchar **)argv, NULL,
                      G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
                      &out, NULL, NULL, &err)) {
        if (g_getenv("ZYNX_DEBUG"))
            g_printerr("spawn %s failed: %s (PATH=%s)\n", argv[0],
                       err ? err->message : "?", g_getenv("PATH"));
        g_clear_error(&err);
        return g_strdup("");
    }
    if (g_getenv("ZYNX_DEBUG"))
        g_printerr("spawn %s -> %zu bytes\n", argv[0], out ? strlen(out) : 0);
    return out ? out : g_strdup("");
}

static gint run_status(const gchar *const *argv)
{
    gint status = -1;
    GError *err = NULL;
    if (!g_spawn_sync(NULL, (gchar **)argv, NULL,
                      G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                          G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, NULL, NULL, &status, &err)) {
        g_clear_error(&err);
        return -1;
    }
    return status;
}

static gchar *human_size(guint64 n)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = (double)n;
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        i++;
    }
    if (i == 0)
        return g_strdup_printf("%" G_GUINT64_FORMAT " B", n);
    return g_strdup_printf("%.1f %s", v, units[i]);
}

/* Read KEY="VALUE" from one lsblk -P line (lsblk escapes " as \x22). */
static gchar *pair_value(const gchar *line, const gchar *key)
{
    gchar *needle = g_strdup_printf("%s=\"", key);
    const gchar *p = strstr(line, needle);
    g_free(needle);
    if (!p)
        return g_strdup("");
    p += strlen(key) + 2;
    const gchar *q = strchr(p, '"');
    if (!q)
        return g_strdup("");

    GString *s = g_string_new(NULL);
    for (const gchar *c = p; c < q; c++) {
        if (c[0] == '\\' && c + 3 < q && c[1] == 'x' &&
            isxdigit((unsigned char)c[2]) && isxdigit((unsigned char)c[3])) {
            char hex[3] = {c[2], c[3], 0};
            g_string_append_c(s, (gchar)strtol(hex, NULL, 16));
            c += 3;
        } else if (c[0] == '\\' && c + 1 < q) {
            g_string_append_c(s, c[1]);
            c++;
        } else {
            g_string_append_c(s, c[0]);
        }
    }
    gchar *res = g_string_free(s, FALSE);
    return res;
}

static void disk_free(gpointer p)
{
    Disk *d = p;
    if (!d)
        return;
    g_free(d->path);
    g_free(d->name);
    g_free(d->model);
    g_free(d->tran);
    g_free(d);
}

static void part_free(gpointer p)
{
    Part *q = p;
    if (!q)
        return;
    g_free(q->path);
    g_free(q->fstype);
    g_free(q);
}

static GPtrArray *load_disks(void)
{
    const gchar *argv[] = {"lsblk", "-P", "-b", "-d", "-o",
                           "NAME,PATH,SIZE,MODEL,TRAN,TYPE", NULL};
    gchar *out = run_capture(argv);
    if (g_getenv("ZYNX_DEBUG"))
        g_printerr("load_disks: %zu bytes\n%s\n", strlen(out), out);
    GPtrArray *arr = g_ptr_array_new_with_free_func(disk_free);
    gchar **lines = g_strsplit(out, "\n", -1);

    for (gchar **l = lines; *l; l++) {
        if (!**l)
            continue;
        gchar *type = pair_value(*l, "TYPE");
        if (g_strcmp0(type, "disk") != 0) {
            g_free(type);
            continue;
        }
        g_free(type);
        Disk *d = g_new0(Disk, 1);
        d->name = pair_value(*l, "NAME");
        d->path = pair_value(*l, "PATH");
        d->model = pair_value(*l, "MODEL");
        d->tran = pair_value(*l, "TRAN");
        if (!d->path[0]) {
            g_free(d->path);
            d->path = g_strdup_printf("/dev/%s", d->name);
        }
        gchar *size = pair_value(*l, "SIZE");
        d->size = g_ascii_strtoull(size, NULL, 10);
        g_free(size);
        if (d->size > 0)
            g_ptr_array_add(arr, d);
        else
            disk_free(d);
    }
    g_strfreev(lines);
    g_free(out);
    return arr;
}

static GPtrArray *load_parts(const gchar *disk)
{
    const gchar *argv[] = {"lsblk", "-P", "-b", "-o",
                           "PATH,SIZE,FSTYPE,TYPE", disk, NULL};
    gchar *out = run_capture(argv);
    GPtrArray *arr = g_ptr_array_new_with_free_func(part_free);
    gchar **lines = g_strsplit(out, "\n", -1);

    for (gchar **l = lines; *l; l++) {
        if (!**l)
            continue;
        gchar *type = pair_value(*l, "TYPE");
        if (g_strcmp0(type, "part") != 0) {
            g_free(type);
            continue;
        }
        g_free(type);
        Part *p = g_new0(Part, 1);
        p->path = pair_value(*l, "PATH");
        p->fstype = pair_value(*l, "FSTYPE");
        gchar *size = pair_value(*l, "SIZE");
        p->size = g_ascii_strtoull(size, NULL, 10);
        g_free(size);
        g_ptr_array_add(arr, p);
    }
    g_strfreev(lines);
    g_free(out);
    return arr;
}

static gboolean is_linux_fs(const gchar *fs)
{
    return fs && (g_strcmp0(fs, "ext4") == 0 || g_strcmp0(fs, "ext3") == 0 ||
                  g_strcmp0(fs, "btrfs") == 0 || g_strcmp0(fs, "xfs") == 0 ||
                  g_strcmp0(fs, "f2fs") == 0);
}

/* Largest Linux filesystem on the disk, or NULL. */
static Part *find_root_part(const gchar *disk)
{
    GPtrArray *parts = load_parts(disk);
    Part *best = NULL;
    for (guint i = 0; i < parts->len; i++) {
        Part *p = g_ptr_array_index(parts, i);
        if (is_linux_fs(p->fstype) && (!best || p->size > best->size))
            best = p;
    }
    Part *copy = NULL;
    if (best) {
        copy = g_new0(Part, 1);
        copy->path = g_strdup(best->path);
        copy->fstype = g_strdup(best->fstype);
        copy->size = best->size;
    }
    g_ptr_array_free(parts, TRUE);
    return copy;
}

static gchar *disk_partitions_text(const gchar *disk)
{
    GPtrArray *parts = load_parts(disk);
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < parts->len; i++) {
        Part *p = g_ptr_array_index(parts, i);
        gchar *h = human_size(p->size);
        const gchar *base = strrchr(p->path, '/');
        base = base ? base + 1 : p->path;
        if (s->len)
            g_string_append(s, "   ·   ");
        g_string_append_printf(s, "%s  %s  %s", base, h,
                               p->fstype[0] ? p->fstype : "raw");
        g_free(h);
    }
    if (!s->len)
        g_string_append(s, "No partitions");
    g_ptr_array_free(parts, TRUE);
    return g_string_free(s, FALSE);
}

static gchar *detect_live_disk(void)
{
    const gchar *targets[] = {"/run/archiso/bootmnt", "/run/archiso/copytoram",
                              "/run/archiso/cowspace", NULL};
    for (int i = 0; targets[i]; i++) {
        const gchar *fm[] = {"findmnt", "-n", "-o", "SOURCE", "--target",
                             targets[i], NULL};
        gchar *src = run_capture(fm);
        g_strstrip(src);
        if (!src[0]) {
            g_free(src);
            continue;
        }
        const gchar *pk[] = {"lsblk", "-ndo", "PKNAME", src, NULL};
        gchar *parent = run_capture(pk);
        g_strstrip(parent);
        gchar *res = NULL;
        if (parent[0])
            res = g_strdup_printf("/dev/%s", parent);
        else if (g_str_has_prefix(src, "/dev/"))
            res = g_strdup(src);
        g_free(parent);
        g_free(src);
        if (res)
            return res;
    }
    return NULL;
}

/* NULL when root is not available, empty array when already root. */
static GPtrArray *priv_prefix(void)
{
    GPtrArray *p = g_ptr_array_new_with_free_func(g_free);
    if (geteuid() == 0)
        return p;

    if (g_find_program_in_path("sudo")) {
        const gchar *probe[] = {"sudo", "-n", "true", NULL};
        if (run_status(probe) == 0) {
            g_ptr_array_add(p, g_strdup("sudo"));
            g_ptr_array_add(p, g_strdup("-n"));
            return p;
        }
    }
    if (g_find_program_in_path("pkexec")) {
        g_ptr_array_add(p, g_strdup("pkexec"));
        return p;
    }
    g_ptr_array_free(p, TRUE);
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* UI helpers                                                                */
/* ------------------------------------------------------------------------- */
static GtkWidget *label_new(const gchar *text, const gchar *cls, gfloat xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls)
        gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

static GtkWidget *heading_new(const gchar *text, const gchar *sub)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(box), label_new(text, "h1", 0), FALSE, FALSE, 0);
    if (sub) {
        GtkWidget *s = label_new(sub, "dim", 0);
        gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
        gtk_box_pack_start(GTK_BOX(box), s, FALSE, FALSE, 0);
    }
    return box;
}

static GtkWidget *card_new(GtkOrientation o, gint spacing)
{
    GtkWidget *b = gtk_box_new(o, spacing);
    gtk_style_context_add_class(gtk_widget_get_style_context(b), "card");
    return b;
}

static GtkWidget *page_new(void)
{
    GtkWidget *p = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(p, 34);
    gtk_widget_set_margin_end(p, 34);
    gtk_widget_set_margin_top(p, 24);
    gtk_widget_set_margin_bottom(p, 16);
    return p;
}

static GtkWidget *hline_new(void)
{
    GtkWidget *s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(s, 4);
    gtk_widget_set_margin_bottom(s, 4);
    return s;
}

static const gchar *edition_short(const gchar *id)
{
    if (g_strcmp0(id, "minimal") == 0)
        return "Minimal";
    if (g_strcmp0(id, "console") == 0)
        return "Console";
    return "Full";
}

static void update_status(App *a)
{
    const gchar *base = a->disk_path ? strrchr(a->disk_path, '/') : NULL;
    base = base ? base + 1 : (a->disk_path ? a->disk_path : "no disk");
    gchar *s = g_strdup_printf("%s   ·   %s   ·   %s   ·   %s",
                               base, a->do_format ? "Format" : "Keep data",
                               edition_short(a->edition), a->shell);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(a->header), s);
    g_free(s);
}

/* ------------------------------------------------------------------------- */
/* Pages                                                                     */
/* ------------------------------------------------------------------------- */
static GtkWidget *build_welcome(App *a)
{
    (void)a;
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);

    GtkWidget *brand = label_new("ZYNX", "brand", 0.5f);
    gtk_box_pack_start(GTK_BOX(page), brand, FALSE, FALSE, 0);
    GtkWidget *sub = label_new("LINUX", "brand-sub", 0.5f);
    gtk_box_pack_start(GTK_BOX(page), sub, FALSE, FALSE, 4);

    GtkWidget *desc = gtk_label_new(
        "A clean Arch Linux system with the XFCE desktop.\n"
        "Pick an edition, a disk and a few options — the installer\n"
        "does the rest offline.");
    gtk_label_set_justify(GTK_LABEL(desc), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.5f);
    gtk_style_context_add_class(gtk_widget_get_style_context(desc), "dim");
    gtk_widget_set_margin_top(desc, 24);
    gtk_box_pack_start(GTK_BOX(page), desc, FALSE, FALSE, 0);

    GtkWidget *steps = label_new("Edition   ·   Disk   ·   User   ·   Settings", "hint", 0.5f);
    gtk_widget_set_margin_top(steps, 26);
    gtk_box_pack_start(GTK_BOX(page), steps, FALSE, FALSE, 0);

    GtkWidget *sysrq = label_new(
        "If the machine freezes: hold Alt + SysRq and press R E I S U B.", "hint", 0.5f);
    gtk_widget_set_margin_top(sysrq, 18);
    gtk_box_pack_start(GTK_BOX(page), sysrq, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);
    return page;
}

static void on_edition_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
    (void)box;
    App *a = data;
    if (!row)
        return;
    const gchar *id = g_object_get_data(G_OBJECT(row), "edition-id");
    if (id) {
        g_free(a->edition);
        a->edition = g_strdup(id);
        update_status(a);
    }
}

static GtkWidget *edition_row(const gchar *id, const gchar *title,
                              const gchar *text)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 9);
    gtk_widget_set_margin_bottom(box, 9);
    gtk_box_pack_start(GTK_BOX(box), label_new(title, "h2", 0), FALSE, FALSE, 0);
    GtkWidget *d = label_new(text, "hint", 0);
    gtk_label_set_line_wrap(GTK_LABEL(d), TRUE);
    gtk_box_pack_start(GTK_BOX(box), d, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(row), box);
    g_object_set_data_full(G_OBJECT(row), "edition-id", g_strdup(id), g_free);
    return row;
}

static GtkWidget *build_edition(App *a)
{
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page),
                       heading_new("Edition",
                                   "Choose what the installed system should include."),
                       FALSE, FALSE, 0);

    GtkWidget *card = card_new(GTK_ORIENTATION_VERTICAL, 4);
    a->edition_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(a->edition_list),
                                    GTK_SELECTION_SINGLE);
    gtk_list_box_insert(GTK_LIST_BOX(a->edition_list),
                        edition_row("full", "XFCE Full",
                                    "Complete desktop: Firefox, LibreOffice, GIMP, VLC, "
                                    "printing and media support."),
                        -1);
    gtk_list_box_insert(GTK_LIST_BOX(a->edition_list),
                        edition_row("minimal", "XFCE Minimal",
                                    "XFCE with essential applications only — no office, "
                                    "graphics or media suites."),
                        -1);
    gtk_list_box_insert(GTK_LIST_BOX(a->edition_list),
                        edition_row("console", "Console",
                                    "No graphical desktop: text login, networking and "
                                    "package tools."),
                        -1);
    g_signal_connect(a->edition_list, "row-selected",
                     G_CALLBACK(on_edition_selected), a);
    gtk_box_pack_start(GTK_BOX(card), a->edition_list, TRUE, TRUE, 0);
    gtk_list_box_select_row(GTK_LIST_BOX(a->edition_list),
                            gtk_list_box_get_row_at_index(GTK_LIST_BOX(a->edition_list), 0));
    gtk_box_pack_start(GTK_BOX(page), card, FALSE, FALSE, 0);

    GtkWidget *shell_card = card_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *txt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_pack_start(GTK_BOX(txt), label_new("Login shell", "h2", 0), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(txt),
                       label_new("Shell for the new user account.", "hint", 0),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shell_card), txt, TRUE, TRUE, 0);
    a->shell_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->shell_combo), "bash", "bash");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->shell_combo), "zsh", "zsh");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->shell_combo), "fish", "fish");
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->shell_combo), 0);
    gtk_widget_set_valign(a->shell_combo, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(shell_card), a->shell_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), shell_card, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);
    return page;
}

static void on_format_toggled(GtkSwitch *sw, GParamSpec *pspec, gpointer data);
static void on_disk_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data);
static void refresh_disk_list(App *a);

static GtkWidget *build_disk(App *a)
{
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page),
                       heading_new("Disk",
                                   "Select the drive that will hold Zynx Linux."),
                       FALSE, FALSE, 0);

    GtkWidget *card = card_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(top),
                       label_new("Available drives", "h2", 0), TRUE, TRUE, 0);
    GtkWidget *refresh = gtk_button_new_with_label("Refresh");
    gtk_style_context_add_class(gtk_widget_get_style_context(refresh), "ghost");
    g_signal_connect_swapped(refresh, "clicked", G_CALLBACK(refresh_disk_list), a);
    gtk_box_pack_end(GTK_BOX(top), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), top, FALSE, FALSE, 0);

    a->disk_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(a->disk_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(a->disk_scroll, -1, 176);
    a->disk_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(a->disk_list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(a->disk_list, "row-selected", G_CALLBACK(on_disk_selected), a);
    gtk_container_add(GTK_CONTAINER(a->disk_scroll), a->disk_list);
    gtk_box_pack_start(GTK_BOX(card), a->disk_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), card, FALSE, FALSE, 0);

    GtkWidget *fmt = card_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *fmt_txt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_pack_start(GTK_BOX(fmt_txt), label_new("Format the disk", "h2", 0),
                       FALSE, FALSE, 0);
    a->format_hint = label_new("The partition table is kept; existing files are not erased.",
                               "hint", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->format_hint), TRUE);
    gtk_box_pack_start(GTK_BOX(fmt_txt), a->format_hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fmt), fmt_txt, TRUE, TRUE, 0);

    a->format_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(a->format_switch), FALSE);
    gtk_widget_set_valign(a->format_switch, GTK_ALIGN_CENTER);
    g_signal_connect(a->format_switch, "notify::active",
                     G_CALLBACK(on_format_toggled), a);
    gtk_box_pack_end(GTK_BOX(fmt), a->format_switch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), fmt, FALSE, FALSE, 0);

    a->disk_note = label_new("", "danger", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->disk_note), TRUE);
    gtk_box_pack_start(GTK_BOX(page), a->disk_note, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);
    return page;
}

static GtkWidget *build_account(App *a)
{
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page),
                       heading_new("User",
                                   "The account you will use every day."),
                       FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(grid), "card");

    a->user_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(a->user_entry), "zynx");
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->user_entry), "for example alex");
    a->pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(a->pass_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(a->pass_entry), GTK_INPUT_PURPOSE_PASSWORD);
    a->pass2_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(a->pass2_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(a->pass2_entry), GTK_INPUT_PURPOSE_PASSWORD);
    a->root_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(a->root_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(a->root_entry), GTK_INPUT_PURPOSE_PASSWORD);
    a->host_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(a->host_entry), "zynx");

    struct {
        const gchar *label;
        GtkWidget *widget;
        const gchar *hint;
    } rows[] = {
        {"Username", a->user_entry, NULL},
        {"Password", a->pass_entry, NULL},
        {"Confirm password", a->pass2_entry, NULL},
        {"Root password", a->root_entry,
         "optional — leave blank to lock root and use sudo"},
        {"Computer name", a->host_entry, NULL},
    };

    int row = 0;
    for (guint i = 0; i < G_N_ELEMENTS(rows); i++) {
        GtkWidget *l = label_new(rows[i].label, "hint", 0);
        gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request(l, 150, -1);
        gtk_widget_set_hexpand(rows[i].widget, TRUE);
        gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), rows[i].widget, 1, row, 1, 1);
        row++;
        if (rows[i].hint) {
            GtkWidget *h = label_new(rows[i].hint, "hint", 0);
            gtk_label_set_line_wrap(GTK_LABEL(h), TRUE);
            gtk_grid_attach(GTK_GRID(grid), h, 1, row, 1, 1);
            row++;
        }
    }
    gtk_box_pack_start(GTK_BOX(page), grid, FALSE, FALSE, 0);

    a->account_error = label_new("", "error", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->account_error), TRUE);
    gtk_box_pack_start(GTK_BOX(page), a->account_error, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);
    return page;
}

static GtkWidget *combo_row(const gchar *title, const gchar *hint)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(box), label_new(title, "hint", 0), FALSE, FALSE, 0);
    if (hint)
        gtk_box_pack_start(GTK_BOX(box), label_new(hint, "hint", 0), FALSE, FALSE, 0);
    return box;
}

static GtkWidget *build_settings(App *a)
{
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page),
                       heading_new("Settings",
                                   "Language, time, keyboard and the disk layout."),
                       FALSE, FALSE, 0);

    GtkWidget *card = card_new(GTK_ORIENTATION_VERTICAL, 12);

    GtkWidget *tz = combo_row("Time zone", NULL);
    a->tz_combo = gtk_combo_box_text_new_with_entry();
    const gchar *zones[] = {
        "UTC", "Europe/London", "Europe/Berlin", "Europe/Paris",
        "Europe/Moscow", "Europe/Kyiv", "Europe/Minsk", "Asia/Almaty",
        "Asia/Tbilisi", "Asia/Yerevan", "Asia/Yekaterinburg",
        "Asia/Novosibirsk", "Asia/Vladivostok", "Asia/Tokyo",
        "Asia/Shanghai", "America/New_York", "America/Los_Angeles",
        "Australia/Sydney", NULL};
    for (int i = 0; zones[i]; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a->tz_combo), zones[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->tz_combo), 0);
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(gtk_bin_get_child(GTK_BIN(a->tz_combo))), "Europe/Moscow");
    gtk_box_pack_start(GTK_BOX(tz), a->tz_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), tz, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), hline_new(), FALSE, FALSE, 0);

    GtkWidget *loc = combo_row("System language", NULL);
    a->locale_combo = gtk_combo_box_text_new();
    struct { const gchar *id, *name; } locales[] = {
        {"en_US.UTF-8", "English (United States)"},
        {"en_GB.UTF-8", "English (United Kingdom)"},
        {"de_DE.UTF-8", "Deutsch"},
        {"fr_FR.UTF-8", "Français"},
        {"es_ES.UTF-8", "Español"},
        {"it_IT.UTF-8", "Italiano"},
        {"pt_BR.UTF-8", "Português (Brasil)"},
        {"pl_PL.UTF-8", "Polski"},
        {"ru_RU.UTF-8", "Русский"},
        {"uk_UA.UTF-8", "Українська"},
        {"tr_TR.UTF-8", "Türkçe"},
        {"zh_CN.UTF-8", "中文"},
        {"ja_JP.UTF-8", "日本語"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(locales); i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->locale_combo),
                                  locales[i].id, locales[i].name);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(a->locale_combo), "en_US.UTF-8");
    gtk_box_pack_start(GTK_BOX(loc), a->locale_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), loc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), hline_new(), FALSE, FALSE, 0);

    GtkWidget *kbd = combo_row("Keyboard layout", NULL);
    a->keyboard_combo = gtk_combo_box_text_new();
    struct { const gchar *id, *name; } keyboards[] = {
        {"us", "US"}, {"us,ru", "US / RU"}, {"ru", "RU"},
        {"de", "DE"}, {"fr", "FR"}, {"es", "ES"}, {"it", "IT"},
        {"pl", "PL"}, {"ua", "UA"}, {"tr", "TR"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(keyboards); i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->keyboard_combo),
                                  keyboards[i].id, keyboards[i].name);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(a->keyboard_combo), "us");
    gtk_box_pack_start(GTK_BOX(kbd), a->keyboard_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), kbd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), hline_new(), FALSE, FALSE, 0);

    GtkWidget *sizes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(root_box), label_new("Root partition size", "hint", 0),
                       FALSE, FALSE, 0);
    a->root_spin = gtk_spin_button_new_with_range(20, 1000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->root_spin), 40);
    gtk_box_pack_start(GTK_BOX(root_box), a->root_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sizes), root_box, TRUE, TRUE, 0);

    GtkWidget *swap_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(swap_box), label_new("Swap file", "hint", 0),
                       FALSE, FALSE, 0);
    a->swap_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->swap_combo), "0", "None");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->swap_combo), "2", "2 GiB");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->swap_combo), "4", "4 GiB");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(a->swap_combo), "8", "8 GiB");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(a->swap_combo), "4");
    gtk_box_pack_start(GTK_BOX(swap_box), a->swap_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sizes), swap_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(card), sizes, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), hline_new(), FALSE, FALSE, 0);

    a->default_check = gtk_check_button_new_with_label(
        "Make Zynx the default boot entry");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->default_check), TRUE);
    gtk_box_pack_start(GTK_BOX(card), a->default_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card),
                       label_new("When unchecked, the current boot order is left alone.",
                                 "hint", 0),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), card, FALSE, FALSE, 0);
    a->settings_error = label_new("", "error", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->settings_error), TRUE);
    gtk_box_pack_start(GTK_BOX(page), a->settings_error, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);
    return page;
}

static GtkWidget *build_summary(App *a)
{
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page),
                       heading_new("Review",
                                   "Check the settings. They cannot be changed during "
                                   "installation."),
                       FALSE, FALSE, 0);
    a->summary_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(a->summary_grid), 24);
    gtk_grid_set_row_spacing(GTK_GRID(a->summary_grid), 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(a->summary_grid), "card");
    gtk_box_pack_start(GTK_BOX(page), a->summary_grid, FALSE, FALSE, 0);

    a->summary_plan = label_new("", "hint", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->summary_plan), TRUE);
    gtk_box_pack_start(GTK_BOX(page), a->summary_plan, FALSE, FALSE, 0);
    a->summary_note = label_new("", "danger", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->summary_note), TRUE);
    gtk_box_pack_start(GTK_BOX(page), a->summary_note, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);
    return page;
}

static GtkWidget *build_install(App *a)
{
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page),
                       heading_new("Installing", "Do not power off the computer."),
                       FALSE, FALSE, 0);

    a->install_status = label_new("Preparing…", "h2", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->install_status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), a->install_status, FALSE, FALSE, 0);

    a->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(a->progress), 0.08);
    gtk_box_pack_start(GTK_BOX(page), a->progress, FALSE, FALSE, 0);

    a->install_hint = label_new("Copying the system usually takes a few minutes.",
                                "hint", 0);
    gtk_box_pack_start(GTK_BOX(page), a->install_hint, FALSE, FALSE, 0);

    a->log_expander = gtk_expander_new("Details");
    a->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(a->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(a->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(a->log_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(a->log_view), GTK_WRAP_WORD_CHAR);
    a->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->log_view));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 190);
    gtk_container_add(GTK_CONTAINER(scroll), a->log_view);
    gtk_container_add(GTK_CONTAINER(a->log_expander), scroll);
    gtk_expander_set_expanded(GTK_EXPANDER(a->log_expander), FALSE);
    gtk_box_pack_start(GTK_BOX(page), a->log_expander, TRUE, TRUE, 0);
    return page;
}

static GtkWidget *build_done(App *a)
{
    GtkWidget *page = page_new();
    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);

    a->done_icon = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(a->done_icon),
                         "<span foreground=\"#ff8c00\" size=\"42000\">✓</span>");
    gtk_label_set_xalign(GTK_LABEL(a->done_icon), 0.5f);
    gtk_box_pack_start(GTK_BOX(page), a->done_icon, FALSE, FALSE, 0);

    a->done_title = label_new("Done", "h1", 0.5f);
    gtk_box_pack_start(GTK_BOX(page), a->done_title, FALSE, FALSE, 0);

    a->done_text = label_new("", "dim", 0.5f);
    gtk_label_set_justify(GTK_LABEL(a->done_text), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(page), a->done_text, FALSE, FALSE, 0);

    a->reboot_btn = gtk_button_new_with_label("Reboot");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->reboot_btn), "ghost");
    gtk_widget_set_halign(a->reboot_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(a->reboot_btn, 16);
    gtk_box_pack_start(GTK_BOX(page), a->reboot_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                       TRUE, TRUE, 0);
    return page;
}

/* ------------------------------------------------------------------------- */
/* Disk page logic                                                           */
/* ------------------------------------------------------------------------- */
static void update_format_note(App *a)
{
    if (a->do_format) {
        gtk_label_set_text(GTK_LABEL(a->format_hint),
                           "The drive will be wiped and repartitioned.");
        if (a->disk_path && a->disk_size) {
            gchar *h = human_size(a->disk_size);
            gchar *t = g_strdup_printf(
                "All data on %s (%s) will be permanently destroyed.", a->disk_path, h);
            gtk_label_set_text(GTK_LABEL(a->disk_note), t);
            g_free(t);
            g_free(h);
        } else {
            gtk_label_set_text(GTK_LABEL(a->disk_note),
                               "All data on the selected drive will be destroyed.");
        }
    } else {
        gtk_label_set_text(GTK_LABEL(a->format_hint),
                           "The partition table is kept; existing files are not erased.");
        if (!a->disk_path) {
            gtk_label_set_text(GTK_LABEL(a->disk_note), "Select a drive.");
            return;
        }
        gchar *text = disk_partitions_text(a->disk_path);
        Part *root = find_root_part(a->disk_path);
        if (root) {
            gchar *h = human_size(root->size);
            gchar *t = g_strdup_printf(
                "Existing layout: %s\nWill install into %s (%s, %s); /home and other "
                "partitions are preserved.",
                text, root->path, root->fstype ? root->fstype : "linux", h);
            gtk_label_set_text(GTK_LABEL(a->disk_note), t);
            g_free(t);
            g_free(h);
            g_free(root->path);
            g_free(root->fstype);
            g_free(root);
        } else {
            gchar *t = g_strdup_printf(
                "Existing layout: %s\nNo Linux root filesystem found — enable "
                "formatting or choose another drive.", text);
            gtk_label_set_text(GTK_LABEL(a->disk_note), t);
            g_free(t);
        }
        g_free(text);
    }
}

static void refresh_disk_list(App *a)
{
    GList *rows = gtk_container_get_children(GTK_CONTAINER(a->disk_list));
    for (GList *it = rows; it; it = it->next)
        gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(rows);

    GPtrArray *disks = load_disks();
    gchar *live = detect_live_disk();
    gboolean show_all = g_getenv("ZYNX_SHOW_ALL_DISKS") != NULL;
    int shown = 0;

    for (guint i = 0; i < disks->len; i++) {
        Disk *d = g_ptr_array_index(disks, i);
        if (live && g_strcmp0(live, d->path) == 0)
            continue;
        if (!show_all && d->size < (guint64)MIN_DISK_GIB * 1024 * 1024 * 1024)
            continue;

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_start(box, 8);
        gtk_widget_set_margin_end(box, 8);
        gtk_widget_set_margin_top(box, 7);
        gtk_widget_set_margin_bottom(box, 7);

        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(left), label_new(d->path, "h2", 0), FALSE, FALSE, 0);
        gchar *meta = g_strdup_printf("%s%s%s", d->model[0] ? d->model : "Drive",
                                      d->tran[0] ? "  ·  " : "",
                                      d->tran[0] ? d->tran : "");
        gtk_box_pack_start(GTK_BOX(left), label_new(meta, "hint", 0), FALSE, FALSE, 0);
        g_free(meta);
        gtk_box_pack_start(GTK_BOX(line), left, TRUE, TRUE, 0);
        gchar *h = human_size(d->size);
        gtk_box_pack_end(GTK_BOX(line), label_new(h, "dim", 1), FALSE, FALSE, 0);
        g_free(h);
        gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);

        gchar *parts = disk_partitions_text(d->path);
        GtkWidget *pl = label_new(parts, "hint", 0);
        gtk_label_set_ellipsize(GTK_LABEL(pl), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(box), pl, FALSE, FALSE, 0);
        g_free(parts);

        gtk_container_add(GTK_CONTAINER(row), box);
        g_object_set_data_full(G_OBJECT(row), "disk-path", g_strdup(d->path), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(a->disk_list), row, -1);
        gtk_widget_show_all(row);
        shown++;
    }

    g_ptr_array_free(disks, TRUE);
    g_free(live);

    if (shown == 0) {
        gtk_label_set_text(GTK_LABEL(a->disk_note),
                           "No suitable drives found (12 GiB minimum).");
        return;
    }

    GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(a->disk_list), 0);
    if (first)
        gtk_list_box_select_row(GTK_LIST_BOX(a->disk_list), first);
    update_format_note(a);
}

static void on_disk_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
    (void)box;
    App *a = data;
    if (!row) {
        g_free(a->disk_path);
        a->disk_path = NULL;
        a->disk_size = 0;
        update_status(a);
        return;
    }
    const gchar *path = g_object_get_data(G_OBJECT(row), "disk-path");
    g_free(a->disk_path);
    a->disk_path = g_strdup(path);

    /* size from lsblk directly */
    const gchar *argv[] = {"lsblk", "-bdno", "SIZE", a->disk_path, NULL};
    gchar *out = run_capture(argv);
    g_strstrip(out);
    a->disk_size = g_ascii_strtoull(out, NULL, 10);
    g_free(out);
    update_status(a);
    update_format_note(a);
}

static void on_format_toggled(GtkSwitch *sw, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    App *a = data;
    a->do_format = gtk_switch_get_active(sw);
    update_status(a);
    update_format_note(a);
}

/* ------------------------------------------------------------------------- */
/* Validation                                                                */
/* ------------------------------------------------------------------------- */
static gboolean valid_username(const gchar *s)
{
    if (!s || !*s || strlen(s) > 32)
        return FALSE;
    if (!(g_ascii_islower(s[0]) || s[0] == '_'))
        return FALSE;
    for (const gchar *p = s; *p; p++)
        if (!(g_ascii_islower(*p) || g_ascii_isdigit(*p) || *p == '_' || *p == '-'))
            return FALSE;
    return TRUE;
}

static gboolean valid_hostname(const gchar *s)
{
    if (!s || !*s || strlen(s) > 63)
        return FALSE;
    if (!(g_ascii_islower(s[0]) || g_ascii_isdigit(s[0])))
        return FALSE;
    for (const gchar *p = s; *p; p++)
        if (!(g_ascii_islower(*p) || g_ascii_isdigit(*p) || *p == '-'))
            return FALSE;
    return s[strlen(s) - 1] != '-';
}

static gboolean validate_account(App *a)
{
    GString *err = g_string_new(NULL);
    const gchar *user = gtk_entry_get_text(GTK_ENTRY(a->user_entry));
    const gchar *p1 = gtk_entry_get_text(GTK_ENTRY(a->pass_entry));
    const gchar *p2 = gtk_entry_get_text(GTK_ENTRY(a->pass2_entry));
    const gchar *root = gtk_entry_get_text(GTK_ENTRY(a->root_entry));
    const gchar *host = gtk_entry_get_text(GTK_ENTRY(a->host_entry));

    if (!valid_username(user))
        g_string_append(err, "Username: lowercase letters, digits, “_” and “-”.\n");
    else if (g_strcmp0(user, "root") == 0)
        g_string_append(err, "The name “root” is reserved.\n");
    if (!*p1)
        g_string_append(err, "Enter the user password.\n");
    if (g_strcmp0(p1, p2) != 0)
        g_string_append(err, "The passwords do not match.\n");
    if (*root && strlen(root) < 4)
        g_string_append(err, "Root password: at least 4 characters, or leave it blank.\n");
    if (!valid_hostname(host))
        g_string_append(err, "Computer name: lowercase letters, digits and “-”.\n");

    gtk_label_set_text(GTK_LABEL(a->account_error), err->str);
    gboolean ok = err->len == 0;
    g_string_free(err, TRUE);
    return ok;
}

static gboolean validate_settings(App *a)
{
    GString *err = g_string_new(NULL);
    gchar *tz = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(a->tz_combo));
    if (!tz || !*tz) {
        g_string_append(err, "Choose a time zone.\n");
    } else {
        gchar *path = g_build_filename("/usr/share/zoneinfo", tz, NULL);
        if (!g_file_test(path, G_FILE_TEST_EXISTS))
            g_string_append_printf(err, "Unknown time zone: %s\n", tz);
        g_free(path);
    }
    g_free(tz);
    gtk_label_set_text(GTK_LABEL(a->settings_error), err->str);
    gboolean ok = err->len == 0;
    g_string_free(err, TRUE);
    return ok;
}

static gboolean check_disk_page(App *a)
{
    if (!a->disk_path) {
        gtk_label_set_text(GTK_LABEL(a->disk_note), "Select a drive.");
        return FALSE;
    }
    if (a->disk_size < (guint64)MIN_DISK_GIB * 1024 * 1024 * 1024) {
        gtk_label_set_text(GTK_LABEL(a->disk_note),
                           "The drive is smaller than 12 GiB; Zynx will not fit.");
        return FALSE;
    }
    if (a->do_format) {
        if (a->root_spin)
            a->root_gib = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->root_spin));
        guint64 need = ((guint64)MIN_DISK_GIB + 6) * 1024 * 1024 * 1024;
        if (a->disk_size < need) {
            gtk_label_set_text(GTK_LABEL(a->disk_note),
                               "Not enough space for a root and /home partition.");
            return FALSE;
        }
        guint64 gib = a->disk_size / (1024 * 1024 * 1024);
        if (gib < (guint64)a->root_gib + 6) {
            gchar *t = g_strdup_printf(
                "Root size %d GiB is too large: /home needs at least 5 GiB.", a->root_gib);
            gtk_label_set_text(GTK_LABEL(a->disk_note), t);
            g_free(t);
            return FALSE;
        }
    } else {
        Part *root = find_root_part(a->disk_path);
        if (!root) {
            gtk_label_set_text(GTK_LABEL(a->disk_note),
                               "No Linux root filesystem (ext4/btrfs/xfs/f2fs) found. "
                               "Enable formatting or choose another drive.");
            return FALSE;
        }
        g_free(root->path);
        g_free(root->fstype);
        g_free(root);
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Summary                                                                   */
/* ------------------------------------------------------------------------- */
static void set_summary_row(App *a, int row, const gchar *key, const gchar *value)
{
    GtkWidget *k = label_new(key, "hint", 0);
    GtkWidget *v = label_new(value, "value", 0);
    gtk_label_set_line_wrap(GTK_LABEL(v), TRUE);
    gtk_grid_attach(GTK_GRID(a->summary_grid), k, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(a->summary_grid), v, 1, row, 1, 1);
}

static void refresh_summary(App *a)
{
    GList *kids = gtk_container_get_children(GTK_CONTAINER(a->summary_grid));
    for (GList *it = kids; it; it = it->next)
        gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(kids);

    gchar *tz = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(a->tz_combo));
    const gchar *loc = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->locale_combo));
    const gchar *kbd = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->keyboard_combo));
    const gchar *swap = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->swap_combo));
    a->swap_gib = atoi(swap ? swap : "0");
    a->root_gib = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->root_spin));
    a->set_default = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(a->default_check));
    g_free(a->tz);
    g_free(a->locale);
    g_free(a->keyboard);
    g_free(a->username);
    g_free(a->password);
    g_free(a->root_password);
    g_free(a->hostname);
    g_free(a->shell);
    a->tz = g_strdup(tz ? tz : "UTC");
    a->locale = g_strdup(loc ? loc : "en_US.UTF-8");
    a->keyboard = g_strdup(kbd ? kbd : "us");
    a->username = g_strdup(gtk_entry_get_text(GTK_ENTRY(a->user_entry)));
    a->password = g_strdup(gtk_entry_get_text(GTK_ENTRY(a->pass_entry)));
    a->root_password = g_strdup(gtk_entry_get_text(GTK_ENTRY(a->root_entry)));
    a->hostname = g_strdup(gtk_entry_get_text(GTK_ENTRY(a->host_entry)));
    a->shell = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->shell_combo)));

    gchar *hsize = human_size(a->disk_size);
    gchar *disk_line = g_strdup_printf("%s  ·  %s", a->disk_path, hsize);
    g_free(hsize);

    set_summary_row(a, 0, "Disk", disk_line);
    set_summary_row(a, 1, "Mode", a->do_format ? "Format the disk" : "Keep existing layout");
    set_summary_row(a, 2, "Edition", edition_short(a->edition));
    set_summary_row(a, 3, "Login shell", a->shell);
    set_summary_row(a, 4, "User", a->username);
    set_summary_row(a, 5, "Computer name", a->hostname);
    set_summary_row(a, 6, "Time zone", a->tz);
    set_summary_row(a, 7, "Language", a->locale);
    set_summary_row(a, 8, "Keyboard", a->keyboard);
    set_summary_row(a, 9, "Root password",
                    *a->root_password ? "set" : "locked (use sudo)");
    set_summary_row(a, 10, "Default boot entry",
                    a->set_default ? "Zynx" : "leave unchanged");
    g_free(disk_line);

    if (a->do_format) {
        guint64 gib = a->disk_size / (1024 * 1024 * 1024);
        gint home = (gint)gib - a->root_gib - 1;
        if (home < 5)
            home = 5;
        gchar *plan = g_strdup_printf(
            "Partition plan:  BIOS boot 1 MiB   ·   ESP 512 MiB   ·   "
            "root %d GiB   ·   /home ≈ %d GiB   ·   swap file %d GiB",
            a->root_gib, home, a->swap_gib);
        gtk_label_set_text(GTK_LABEL(a->summary_plan), plan);
        g_free(plan);
        gchar *warn = g_strdup_printf(
            "All data on %s will be erased. This cannot be undone.", a->disk_path);
        gtk_label_set_text(GTK_LABEL(a->summary_note), warn);
        g_free(warn);
    } else {
        Part *root = find_root_part(a->disk_path);
        gchar *plan = NULL;
        if (root) {
            plan = g_strdup_printf(
                "Existing layout is kept. Zynx will be installed into %s (%s). "
                "Swap file: %d GiB.",
                root->path, root->fstype, a->swap_gib);
            g_free(root->path);
            g_free(root->fstype);
            g_free(root);
        } else {
            plan = g_strdup("Existing layout is kept.");
        }
        gtk_label_set_text(GTK_LABEL(a->summary_plan), plan);
        g_free(plan);
        gtk_label_set_text(GTK_LABEL(a->summary_note),
                           "Formatting is disabled: existing files are not erased.");
    }
    gtk_widget_show_all(a->summary_grid);
}

/* ------------------------------------------------------------------------- */
/* Confirmation overlay                                                      */
/* ------------------------------------------------------------------------- */
static GtkWidget *build_confirm(App *a)
{
    GtkWidget *scrim = gtk_event_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(scrim), "confirm-scrim");
    gtk_widget_set_hexpand(scrim, TRUE);
    gtk_widget_set_vexpand(scrim, TRUE);

    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(center, TRUE);
    gtk_widget_set_vexpand(center, TRUE);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "confirm-card");
    gtk_widget_set_size_request(card, 470, -1);
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);

    a->confirm_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(a->confirm_label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->confirm_label), TRUE);
    gtk_box_pack_start(GTK_BOX(card), a->confirm_label, FALSE, FALSE, 0);

    a->confirm_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(card), a->confirm_entry, FALSE, FALSE, 0);

    a->confirm_error = label_new("", "error", 0);
    gtk_label_set_line_wrap(GTK_LABEL(a->confirm_error), TRUE);
    gtk_box_pack_start(GTK_BOX(card), a->confirm_error, FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancel), "ghost");
    GtkWidget *ok = gtk_button_new_with_label("Continue");
    gtk_style_context_add_class(gtk_widget_get_style_context(ok), "primary");
    a->confirm_cancel = cancel;
    a->confirm_ok = ok;
    gtk_box_pack_start(GTK_BOX(buttons), cancel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), ok, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), buttons, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(center), card, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(scrim), center);
    return scrim;
}

static void start_install(App *a);

static void on_confirm_ok(GtkButton *btn, gpointer data)
{
    (void)btn;
    App *a = data;
    const gchar *typed = gtk_entry_get_text(GTK_ENTRY(a->confirm_entry));
    if (g_strcmp0(typed, a->confirm_word) != 0) {
        gchar *msg = g_strdup_printf("Type %s to continue.", a->confirm_word);
        gtk_label_set_text(GTK_LABEL(a->confirm_error), msg);
        g_free(msg);
        return;
    }
    gtk_widget_hide(a->confirm);
    start_install(a);
    show_page(a, "install");
}

static void open_confirm(App *a)
{
    refresh_summary(a);
    g_free(a->confirm_word);
    a->confirm_word = g_strdup(a->do_format ? "ERASE" : "INSTALL");
    gchar *markup;
    if (a->do_format) {
        markup = g_strdup_printf(
            "<b>Drive %s will be completely erased.</b>\n\n"
            "All partitions and files will be deleted.\n\n"
            "Type <b>%s</b> to continue.",
            a->disk_path, a->confirm_word);
    } else {
        markup = g_strdup_printf(
            "<b>Install without formatting.</b>\n\n"
            "The existing partition will be overwritten; /home is preserved.\n\n"
            "Type <b>%s</b> to continue.",
            a->confirm_word);
    }
    gtk_label_set_markup(GTK_LABEL(a->confirm_label), markup);
    g_free(markup);
    gtk_entry_set_text(GTK_ENTRY(a->confirm_entry), "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->confirm_entry), a->confirm_word);
    gtk_label_set_text(GTK_LABEL(a->confirm_error), "");
    gtk_widget_show_all(a->confirm);
    gtk_widget_grab_focus(a->confirm_entry);
}

/* ------------------------------------------------------------------------- */
/* Install run                                                               */
/* ------------------------------------------------------------------------- */
static void append_log(App *a)
{
    gchar *text = NULL;
    if (!g_file_get_contents(log_path, &text, NULL, NULL)) {
        g_free(text);
        return;
    }
    gsize len = strlen(text);
    if (len > a->log_pos) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(a->log_buffer, &end);
        gtk_text_buffer_insert(a->log_buffer, &end, text + a->log_pos, -1);
        GtkTextMark *mark = gtk_text_buffer_create_mark(a->log_buffer, NULL, &end, FALSE);
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(a->log_view), mark);
        gtk_text_buffer_move_mark(a->log_buffer, mark, &end);
        a->log_pos = len;
    }
    g_free(text);
}

static void read_status(App *a)
{
    gchar *text = NULL;
    if (!g_file_get_contents(status_path, &text, NULL, NULL)) {
        g_free(text);
        return;
    }
    gchar **lines = g_strsplit(text, "\n", 3);
    if (lines[0] && lines[1]) {
        int pct = atoi(lines[0]);
        if (lines[1][0])
            gtk_label_set_text(GTK_LABEL(a->install_status), lines[1]);
        if (pct >= 0)
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->progress),
                                          CLAMP(pct, 0, 100) / 100.0);
    }
    g_strfreev(lines);
    g_free(text);
}

static void install_done(App *a)
{
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->progress), 1.0);
    gchar *text = g_strdup_printf(
        "Zynx Linux was installed on %s.\n\n"
        "Remove the installation medium and reboot.\n"
        "Root password: %s",
        a->disk_path,
        *a->root_password ? "set" : "locked (use sudo)");
    gtk_label_set_text(GTK_LABEL(a->done_text), text);
    g_free(text);
    gtk_label_set_markup(GTK_LABEL(a->done_icon),
                         "<span foreground=\"#ff8c00\" size=\"42000\">✓</span>");
    gtk_label_set_text(GTK_LABEL(a->done_title), "Done");
    gtk_widget_show(a->reboot_btn);
    show_page(a, "done");
}

static void install_failed(App *a, const gchar *message)
{
    a->failed = TRUE;
    a->running = FALSE;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->progress), 0.0);
    gchar *t = g_strdup_printf("Installation failed: %s", message);
    gtk_label_set_text(GTK_LABEL(a->install_status), t);
    g_free(t);
    gtk_label_set_text(GTK_LABEL(a->install_hint),
                       "The log is open below. It is also saved as " LOG_FILE ".");
    gtk_expander_set_expanded(GTK_EXPANDER(a->log_expander), TRUE);
    show_page(a, "install");
    gtk_button_set_label(GTK_BUTTON(a->next_btn), "Close");
    gtk_widget_show(a->next_btn);
}

static gboolean poll_install(gpointer data)
{
    App *a = data;
    if (!a->running)
        return G_SOURCE_REMOVE;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(a->progress));
    read_status(a);
    append_log(a);
    if (a->child_seen) {
        a->running = FALSE;
        read_status(a);
        append_log(a);
        if (a->child_code == 0) {
            install_done(a);
        } else {
            const gchar *msg = "the installer exited with an error (see the log)";
            install_failed(a, msg);
        }
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void on_child_exit(GPid pid, gint status, gpointer data)
{
    App *a = data;
    g_spawn_close_pid(pid);
    gint code = 0;
    GError *err = NULL;
    if (!g_spawn_check_wait_status(status, &err)) {
        code = 1;
        if (err && err->message)
            code = 1;
        g_clear_error(&err);
    }
    a->child_code = code;
    a->child_seen = TRUE;
}

static void start_install(App *a)
{
    a->child_seen = FALSE;
    a->child_code = -1;
    a->failed = FALSE;
    a->log_pos = 0;
    gtk_text_buffer_set_text(a->log_buffer, "", -1);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->progress), 0.0);
    gtk_label_set_text(GTK_LABEL(a->install_status), "Preparing…");
    gtk_expander_set_expanded(GTK_EXPANDER(a->log_expander), FALSE);

    GPtrArray *prefix = priv_prefix();
    if (!prefix) {
        install_failed(a, "root privileges are not available (sudo or pkexec).");
        return;
    }

    GPtrArray *argv = g_ptr_array_new();
    for (guint i = 0; i < prefix->len; i++)
        g_ptr_array_add(argv, g_ptr_array_index(prefix, i));

    const gchar *fixed[] = {engine_path, "--auto", NULL};
    for (int i = 0; fixed[i]; i++)
        g_ptr_array_add(argv, (gpointer)fixed[i]);

    g_ptr_array_add(argv, "--disk");
    g_ptr_array_add(argv, a->disk_path);
    g_ptr_array_add(argv, "--user");
    g_ptr_array_add(argv, a->username);
    g_ptr_array_add(argv, "--user-password");
    g_ptr_array_add(argv, a->password);
    g_ptr_array_add(argv, "--root-password");
    g_ptr_array_add(argv, a->root_password);
    g_ptr_array_add(argv, "--hostname");
    g_ptr_array_add(argv, a->hostname);
    g_ptr_array_add(argv, "--timezone");
    g_ptr_array_add(argv, a->tz);
    g_ptr_array_add(argv, "--locale");
    g_ptr_array_add(argv, a->locale);
    g_ptr_array_add(argv, "--keyboard");
    g_ptr_array_add(argv, a->keyboard);
    g_ptr_array_add(argv, "--edition");
    g_ptr_array_add(argv, a->edition);
    g_ptr_array_add(argv, "--shell");
    g_ptr_array_add(argv, a->shell);
    g_ptr_array_add(argv, "--root-size");
    gchar *root_size = g_strdup_printf("%d", a->root_gib);
    g_ptr_array_add(argv, root_size);
    g_ptr_array_add(argv, "--swap");
    gchar *swap = g_strdup_printf("%d", a->swap_gib);
    g_ptr_array_add(argv, swap);
    g_ptr_array_add(argv, "--yes");
    if (a->do_format)
        g_ptr_array_add(argv, "--format");
    else
        g_ptr_array_add(argv, "--no-format");
    if (!a->set_default)
        g_ptr_array_add(argv, "--no-default-boot");
    g_ptr_array_add(argv, NULL);

    GError *err = NULL;
    GPid pid = 0;
    gboolean ok = g_spawn_async(
        NULL, (gchar **)argv->pdata, NULL,
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
            G_SPAWN_STDERR_TO_DEV_NULL,
        NULL, NULL, &pid, &err);

    if (!ok) {
        gchar *msg = g_strdup_printf("could not start the installer: %s",
                                     err ? err->message : "unknown error");
        install_failed(a, msg);
        g_free(msg);
        g_clear_error(&err);
        g_ptr_array_free(prefix, TRUE);
        g_ptr_array_free(argv, TRUE);
        g_free(root_size);
        g_free(swap);
        return;
    }

    a->pid = pid;
    a->running = TRUE;
    g_child_watch_add(pid, on_child_exit, a);
    a->poll_id = g_timeout_add(400, poll_install, a);

    g_ptr_array_free(prefix, TRUE);
    g_ptr_array_free(argv, TRUE);
    g_free(root_size);
    g_free(swap);
}

static void on_reboot(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    GPtrArray *prefix = priv_prefix();
    if (!prefix)
        return;
    GPtrArray *argv = g_ptr_array_new();
    for (guint i = 0; i < prefix->len; i++)
        g_ptr_array_add(argv, g_ptr_array_index(prefix, i));
    g_ptr_array_add(argv, (gpointer)"systemctl");
    g_ptr_array_add(argv, (gpointer)"reboot");
    g_ptr_array_add(argv, NULL);
    GError *err = NULL;
    g_spawn_async(NULL, (gchar **)argv->pdata, NULL,
                  G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    g_clear_error(&err);
    g_ptr_array_free(argv, TRUE);
    g_ptr_array_free(prefix, TRUE);
}

/* ------------------------------------------------------------------------- */
/* Navigation                                                                */
/* ------------------------------------------------------------------------- */
static const gchar *PAGES[] = {"welcome", "edition", "disk", "account", "settings",
                               "summary", NULL};

static void show_page(App *a, const gchar *name)
{
    gtk_stack_set_visible_child_name(GTK_STACK(a->stack), name);
    int idx = -1;
    for (int i = 0; PAGES[i]; i++)
        if (g_strcmp0(PAGES[i], name) == 0)
            idx = i;

    if (idx >= 0) {
        if (idx == 0) {
            update_status(a);
            gtk_widget_hide(a->back_btn);
            gtk_button_set_label(GTK_BUTTON(a->next_btn), "Start");
            gtk_widget_show(a->next_btn);
        } else {
            update_status(a);
            gtk_widget_show(a->back_btn);
            gtk_widget_show(a->next_btn);
            gtk_button_set_label(GTK_BUTTON(a->next_btn),
                                 idx == 5 ? "Install" : "Next");
        }
    } else {
        gtk_widget_hide(a->back_btn);
        gtk_widget_hide(a->next_btn);
        if (strcmp(name, "install") == 0)
            update_status(a);
        if (strcmp(name, "done") == 0) {
            update_status(a);
            gtk_widget_show(a->next_btn);
            gtk_button_set_label(GTK_BUTTON(a->next_btn), "Close");
        }
    }
    if (g_strcmp0(name, "summary") == 0)
        refresh_summary(a);
}

static void on_back(GtkButton *btn, gpointer data)
{
    (void)btn;
    App *a = data;
    const gchar *cur = gtk_stack_get_visible_child_name(GTK_STACK(a->stack));
    for (int i = 0; PAGES[i]; i++) {
        if (g_strcmp0(PAGES[i], cur) == 0 && i > 0) {
            show_page(a, PAGES[i - 1]);
            return;
        }
    }
}

static void on_next(GtkButton *btn, gpointer data)
{
    (void)btn;
    App *a = data;
    const gchar *cur = gtk_stack_get_visible_child_name(GTK_STACK(a->stack));

    if (g_strcmp0(cur, "welcome") == 0) {
        show_page(a, "edition");
    } else if (g_strcmp0(cur, "edition") == 0) {
        show_page(a, "disk");
    } else if (g_strcmp0(cur, "disk") == 0) {
        if (check_disk_page(a))
            show_page(a, "account");
    } else if (g_strcmp0(cur, "account") == 0) {
        if (validate_account(a))
            show_page(a, "settings");
    } else if (g_strcmp0(cur, "settings") == 0) {
        if (validate_settings(a))
            show_page(a, "summary");
    } else if (g_strcmp0(cur, "summary") == 0) {
        open_confirm(a);
    } else if (g_strcmp0(cur, "done") == 0 || g_strcmp0(cur, "install") == 0) {
        gtk_widget_destroy(a->window);
    }
}

/* ------------------------------------------------------------------------- */
/* CSS                                                                       */
/* ------------------------------------------------------------------------- */
static const gchar *CSS =
    "window, .background { background-color: #0e0f12; color: #e8e8ea; }\n"
    "headerbar { background-color: #0e0f12; background-image: none;"
    " border-bottom: 1px solid #1d1f25; box-shadow: none; min-height: 52px; }\n"
    "headerbar .title { color: #e8e8ea; font-weight: 600; }\n"
    "headerbar .subtitle { color: #6f727a; font-size: 11px; }\n"
    "label { color: #e8e8ea; }\n"
    "label.dim { color: #8b8d94; }\n"
    "label.hint { color: #6f727a; font-size: 11px; }\n"
    "label.error { color: #ff6b6b; font-size: 12px; }\n"
    "label.brand { color: #ff8c00; font-size: 52px; font-weight: 800; }\n"
    "label.brand-sub { color: #6f727a; font-size: 12px; }\n"
    "label.h1 { font-size: 21px; font-weight: 700; }\n"
    "label.h2 { font-size: 13px; font-weight: 600; }\n"
    "label.value { color: #e8e8ea; }\n"
    "label.danger { color: #ff8c00; font-size: 12px; }\n"
    ".card { background-color: #15161b; border: 1px solid #1f2128;"
    " border-radius: 14px; padding: 16px; }\n"
    "list { background-color: transparent; background-image: none; }\n"
    "list row { background-color: transparent; border-radius: 10px; margin: 2px 0; }\n"
    "list row:hover { background-color: #1a1c22; }\n"
    "list row:selected { background-color: #241a0c; border: 1px solid #ff8c00; }\n"
    "entry, spinbutton { background-color: #15161b; background-image: none;"
    " color: #e8e8ea; border: 1px solid #23252d; border-radius: 9px;"
    " padding: 7px 10px; min-height: 22px; box-shadow: none; }\n"
    "entry:focus, spinbutton:focus { border-color: #ff8c00; }\n"
    "combobox button { background-color: #15161b; background-image: none;"
    " color: #e8e8ea; border: 1px solid #23252d; border-radius: 9px;"
    " padding: 7px 10px; min-height: 22px; box-shadow: none; }\n"
    "button { background-color: #1a1c22; background-image: none; color: #e8e8ea;"
    " border: 1px solid #23252d; border-radius: 10px; padding: 8px 18px;"
    " box-shadow: none; }\n"
    "button:hover { background-color: #22242c; }\n"
    "button.primary { background-color: #ff8c00; background-image: none;"
    " color: #121212; border: none; font-weight: 700; padding: 9px 24px; }\n"
    "button.primary:hover { background-color: #ffa233; }\n"
    "button.ghost { background-color: transparent; border: 1px solid #23252d;"
    " color: #8b8d94; }\n"
    "button.ghost:hover { color: #e8e8ea; border-color: #3a3d46; }\n"
    "switch { background-color: #23252d; border-radius: 12px; }\n"
    "switch:checked { background-color: #ff8c00; }\n"
    "progressbar trough { background-color: #1a1c22; border-radius: 6px;"
    " min-height: 8px; border: none; }\n"
    "progressbar progress { background-color: #ff8c00; border-radius: 6px;"
    " min-height: 8px; border: none; }\n"
    "textview, textview text { background-color: #101116; color: #8b8d94; }\n"
    "scrolledwindow { border: none; }\n"
    "separator { background-color: #1f2128; min-height: 1px; }\n"
    ".confirm-scrim { background-color: rgba(6, 7, 9, 0.86); }\n"
    ".confirm-card { background-color: #17181d; border: 1px solid #2a2c35;"
    " border-radius: 16px; padding: 22px; }\n";

/* ------------------------------------------------------------------------- */
/* Window / app                                                              */
/* ------------------------------------------------------------------------- */
static App *app_new(GtkApplication *gapp)
{
    App *a = g_new0(App, 1);
    a->app = gapp;
    a->do_format = 0;
    a->set_default = 1;
    a->swap_gib = 4;
    a->root_gib = 40;
    a->edition = g_strdup("full");
    a->shell = g_strdup("bash");
    a->locale = g_strdup("en_US.UTF-8");
    a->keyboard = g_strdup("us");
    a->tz = g_strdup("UTC");
    return a;
}

static void build_window(App *a)
{
    a->window = gtk_application_window_new(a->app);
    gtk_window_set_title(GTK_WINDOW(a->window), APP_TITLE " — Installer");
    gtk_window_set_default_size(GTK_WINDOW(a->window), 820, 660);
    gtk_window_set_resizable(GTK_WINDOW(a->window), FALSE);
    gtk_window_set_position(GTK_WINDOW(a->window), GTK_WIN_POS_CENTER);
    g_signal_connect_swapped(a->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    a->header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(a->header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(a->header), APP_TITLE);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(a->header),
                                "Choose edition, disk and account");
    GtkWidget *brand = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(brand),
                         "<span foreground=\"#ff8c00\" weight=\"bold\">ZYNX</span>");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(a->header), brand);
    gtk_window_set_titlebar(GTK_WINDOW(a->window), a->header);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(a->window), outer);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_box_pack_start(GTK_BOX(outer), overlay, TRUE, TRUE, 0);

    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(overlay), main);

    a->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(a->stack),
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(a->stack), 160);
    gtk_box_pack_start(GTK_BOX(main), a->stack, TRUE, TRUE, 0);

    gtk_stack_add_named(GTK_STACK(a->stack), build_welcome(a), "welcome");
    gtk_stack_add_named(GTK_STACK(a->stack), build_edition(a), "edition");
    gtk_stack_add_named(GTK_STACK(a->stack), build_disk(a), "disk");
    gtk_stack_add_named(GTK_STACK(a->stack), build_account(a), "account");
    gtk_stack_add_named(GTK_STACK(a->stack), build_settings(a), "settings");
    gtk_stack_add_named(GTK_STACK(a->stack), build_summary(a), "summary");
    gtk_stack_add_named(GTK_STACK(a->stack), build_install(a), "install");
    gtk_stack_add_named(GTK_STACK(a->stack), build_done(a), "done");

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(footer, 34);
    gtk_widget_set_margin_end(footer, 34);
    gtk_widget_set_margin_bottom(footer, 20);
    gtk_box_pack_start(GTK_BOX(main), footer, FALSE, FALSE, 0);

    a->back_btn = gtk_button_new_with_label("Back");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->back_btn), "ghost");
    a->next_btn = gtk_button_new_with_label("Next");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->next_btn), "primary");
    gtk_box_pack_start(GTK_BOX(footer), a->back_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footer), a->next_btn, FALSE, FALSE, 0);
    g_signal_connect(a->back_btn, "clicked", G_CALLBACK(on_back), a);
    g_signal_connect(a->next_btn, "clicked", G_CALLBACK(on_next), a);
    g_signal_connect(a->reboot_btn, "clicked", G_CALLBACK(on_reboot), a);

    a->confirm = build_confirm(a);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), a->confirm);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), a->confirm, FALSE);
    gtk_widget_hide(a->confirm);

    g_signal_connect_swapped(a->confirm_cancel, "clicked",
                             G_CALLBACK(gtk_widget_hide), a->confirm);
    g_signal_connect(a->confirm_ok, "clicked", G_CALLBACK(on_confirm_ok), a);
    g_signal_connect(a->confirm_entry, "activate", G_CALLBACK(on_confirm_ok), a);

    gtk_widget_show_all(a->window);
    gtk_widget_hide(a->confirm);
    gtk_widget_hide(a->reboot_btn);

    refresh_disk_list(a);
    show_page(a, "welcome");

    const gchar *test_page = g_getenv("ZYNX_TEST_PAGE");
    if (test_page && gtk_stack_get_child_by_name(GTK_STACK(a->stack), test_page))
        show_page(a, test_page);
}

static void on_activate(GtkApplication *gapp, gpointer data)
{
    (void)data;
    static App *a = NULL;
    if (a) {
        gtk_window_present(GTK_WINDOW(a->window));
        return;
    }
    GtkCssProvider *provider = gtk_css_provider_new();
    GError *err = NULL;
    gtk_css_provider_load_from_data(provider, CSS, -1, &err);
    g_clear_error(&err);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
        g_object_set(settings, "gtk-font-name", "Noto Sans 11", NULL);
    }

    a = app_new(gapp);
    build_window(a);
}

int main(int argc, char **argv)
{
    const gchar *env_engine = g_getenv("ZYNX_ENGINE");
    if (env_engine && *env_engine)
        engine_path = env_engine;
    const gchar *env_log = g_getenv("ZYNX_LOG");
    if (env_log && *env_log)
        log_path = env_log;
    const gchar *env_status = g_getenv("ZYNX_STATUS");
    if (env_status && *env_status)
        status_path = env_status;

    if (!g_file_test(engine_path, G_FILE_TEST_IS_EXECUTABLE)) {
        g_printerr("zynx-gui: %s not found\n", engine_path);
        return 1;
    }
    GtkApplication *gapp = gtk_application_new("org.zynx.installer",
                                               G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gapp, "activate", G_CALLBACK(on_activate), NULL);
    int rc = g_application_run(G_APPLICATION(gapp), argc, argv);
    g_object_unref(gapp);
    return rc;
}
