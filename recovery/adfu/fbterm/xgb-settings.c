/*
 * xgb-settings.c — Graphical System Settings Center for XGB35 (ATM7051H / 256MB DDR3)
 * Tabs:
 *   1. Экран и Мультиэкран (1080p / 720p, single / mirror-hdmi / mirror-lcd / extend)
 *   2. Звук и Микшер (DAC PA Volume, Digital Gain, Speaker switch, I2S vs HDMI output, Sound test)
 *   3. Сеть и Wi-Fi (Turn on Wi-Fi, Scan networks, WPA2 connect + built-in gamepad on-screen keyboard)
 *   4. Процессор, ОЗУ и Питание (Live CPU/RAM/Battery telemetry + 6 CPU profiles + Exit/Reboot)
 */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static GtkWidget *lbl_disp_status;
static GtkWidget *lbl_sys_status;
static GtkWidget *lbl_wifi_status;
static GtkWidget *entry_ssid;
static GtkWidget *entry_pass;
static GtkWidget *wifi_list_box;
static GtkWidget *osk_frame;
static gboolean   osk_shift = FALSE;

static char *run_host_cmd(const char *sh_cmd) {
    char full[1024];
    if (access("/host/bin/busybox", X_OK) == 0) {
        snprintf(full, sizeof(full), "/usr/sbin/chroot /host /bin/busybox sh -c '%s' 2>&1", sh_cmd);
    } else {
        snprintf(full, sizeof(full), "/bin/sh -c '%s' 2>&1", sh_cmd);
    }
    FILE *fp = popen(full, "r");
    if (!fp) return strdup("");
    char *buf = calloc(1, 8192);
    size_t n = fread(buf, 1, 8191, fp);
    buf[n] = '\0';
    pclose(fp);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return buf;
}

static void run_host_bg(const char *sh_cmd) {
    char full[1024];
    if (access("/host/bin/busybox", X_OK) == 0) {
        snprintf(full, sizeof(full), "/usr/sbin/chroot /host /bin/busybox sh -c '%s' >/dev/null 2>&1 &", sh_cmd);
    } else {
        snprintf(full, sizeof(full), "/bin/sh -c '%s' >/dev/null 2>&1 &", sh_cmd);
    }
    int r = system(full);
    (void)r;
}

static int read_int_file(const char *path, int def_val) {
    FILE *f = fopen(path, "r");
    if (!f) return def_val;
    int v = def_val;
    if (fscanf(f, "%d", &v) != 1) v = def_val;
    fclose(f);
    return v;
}

static void read_str_file(const char *path, char *out, size_t sz, const char *def_val) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(out, sz, "%s", def_val);
        return;
    }
    if (!fgets(out, (int)sz, f)) snprintf(out, sz, "%s", def_val);
    fclose(f);
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
}

/* ==================== TAB 1: DISPLAY & MULTI-SCREEN ==================== */

static void refresh_disp_status(void) {
    char res[32], dmode[32], hmode[64];
    read_str_file("/tmp/desktop_res", res, sizeof(res), "1080p");
    read_str_file("/tmp/display_mode", dmode, sizeof(dmode), "single");
    read_str_file("/sys/devices/b02c0000.hdmi/panel.1/owl_panel/hdmi/mode", hmode, sizeof(hmode), "1920x1080p-60Hz");
    char txt[512];
    snprintf(txt, sizeof(txt),
             "<b>Текущий видеорежим HDMI:</b> %s   |   <b>Профиль:</b> %s   |   <b>Мультиэкран:</b> %s",
             hmode, res, dmode);
    gtk_label_set_markup(GTK_LABEL(lbl_disp_status), txt);
}

static void write_simple_file(const char *path, const char *val) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(val, f);
        fclose(f);
    }
}

static void on_set_res(GtkWidget *w, gpointer data) {
    (void)w;
    const char *res = (const char *)data;
    write_simple_file("/tmp/desktop_res", res);
    write_simple_file("/tmp/desktop_relaunch", "1\n");
    int r = system("killall jwm openbox 2>/dev/null");
    (void)r;
}

static void on_set_multiscreen(GtkWidget *w, gpointer data) {
    (void)w;
    const char *mode = (const char *)data;
    char cur_dual[32];
    read_str_file("/tmp/desktop_dualscreen", cur_dual, sizeof(cur_dual), "single");

    if (strcmp(mode, "extend") == 0) {
        write_simple_file("/tmp/desktop_dualscreen", "extend\n");
        write_simple_file("/tmp/desktop_relaunch", "1\n");
        int r = system("killall jwm openbox 2>/dev/null");
        (void)r;
        return;
    }

    if (strcmp(cur_dual, "extend") == 0) {
        unlink("/tmp/desktop_dualscreen");
        write_simple_file("/tmp/desktop_relaunch", "1\n");
        int r = system("killall jwm openbox 2>/dev/null");
        (void)r;
        return;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "/bin/display-mode %s", mode);
    char *out = run_host_cmd(cmd);
    free(out);
    usleep(150000);
    refresh_disp_status();
}

static GtkWidget *build_display_tab(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 14);

    lbl_disp_status = gtk_label_new("");
    gtk_widget_set_halign(lbl_disp_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_disp_status, FALSE, FALSE, 2);

    GtkWidget *fr_res = gtk_frame_new("Разрешение внешнего экрана HDMI (перезапускает сеанс за 2 сек)");
    GtkWidget *hb_res = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(hb_res), 10);

    GtkWidget *b1080 = gtk_button_new_with_label("Full HD  1920 × 1080 @ 60 Гц");
    GtkWidget *b720  = gtk_button_new_with_label("HD  1280 × 720 @ 60 Гц (быстрее 3D)");
    gtk_widget_set_size_request(b1080, -1, 42);
    gtk_widget_set_size_request(b720, -1, 42);
    g_signal_connect(b1080, "clicked", G_CALLBACK(on_set_res), "1080p\n");
    g_signal_connect(b720,  "clicked", G_CALLBACK(on_set_res), "720p\n");
    gtk_box_pack_start(GTK_BOX(hb_res), b1080, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hb_res), b720,  TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(fr_res), hb_res);
    gtk_box_pack_start(GTK_BOX(vbox), fr_res, FALSE, FALSE, 0);

    GtkWidget *fr_ms = gtk_frame_new("Режим работы двух экранов (внешний HDMI + встроенный 3.5\" IPS)");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    struct { const char *label; const char *mode; int col; int row; } modes[] = {
        {"1. Только внешний монитор (HDMI, 3.5\" выключен)", "single",      0, 0},
        {"2. Дублировать HDMI → на маленький 3.5\" экран",   "mirror-hdmi", 1, 0},
        {"3. Дублировать 3.5\" (480×320) → на весь HDMI",    "mirror-lcd",  0, 1},
        {"4. Расширить рабочий стол (Dual-Screen как в ПК)", "extend",      1, 1},
    };
    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(modes[i].label);
        gtk_widget_set_hexpand(btn, TRUE);
        gtk_widget_set_size_request(btn, -1, 46);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_set_multiscreen), (gpointer)modes[i].mode);
        gtk_grid_attach(GTK_GRID(grid), btn, modes[i].col, modes[i].row, 1, 1);
    }
    gtk_container_add(GTK_CONTAINER(fr_ms), grid);
    gtk_box_pack_start(GTK_BOX(vbox), fr_ms, FALSE, FALSE, 0);

    refresh_disp_status();
    return vbox;
}

/* ==================== TAB 2: AUDIO & MIXER ==================== */

static void on_pa_vol_changed(GtkRange *range, gpointer data) {
    (void)data;
    int v = (int)gtk_range_get_value(range);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "/bin/tinymix 15 %d", v);
    run_host_bg(cmd);
}

static void on_dac_gain_changed(GtkRange *range, gpointer data) {
    (void)data;
    int v = (int)gtk_range_get_value(range);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "/bin/tinymix 16 %d && /bin/tinymix 17 %d", v, v);
    run_host_bg(cmd);
}

static void on_spk_toggled(GtkToggleButton *btn, gpointer data) {
    (void)data;
    gboolean on = gtk_toggle_button_get_active(btn);
    run_host_bg(on ? "/bin/tinymix 35 1 && /bin/tinymix 18 1 && /bin/tinymix 19 1"
                   : "/bin/tinymix 35 0");
}

static void on_audio_route(GtkWidget *w, gpointer data) {
    (void)w;
    const char *route = (const char *)data;
    if (strcmp(route, "i2s") == 0) {
        run_host_bg("/bin/tinymix 28 i2s && /bin/tinymix 35 1 && /bin/tinymix 18 1 && /bin/tinymix 19 1");
    } else {
        run_host_bg("/bin/tinymix 28 hdmi && /bin/tinymix 35 0");
    }
}

static void on_sound_test(GtkWidget *w, gpointer data) {
    (void)w;
    const char *card = (const char *)data;
    char cmd[128];
    if (strcmp(card, "0") == 0) {
        snprintf(cmd, sizeof(cmd), "/bin/tinymix 28 i2s; /bin/tinymix 35 1; /bin/hwtest sound 0");
    } else {
        snprintf(cmd, sizeof(cmd), "/bin/hwtest sound 1");
    }
    run_host_bg(cmd);
}

static GtkWidget *build_audio_tab(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 14);

    GtkWidget *fr_vol = gtk_frame_new("Уровни громкости аудиокодека ATC2603C");
    GtkWidget *vb_vol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vb_vol), 10);

    GtkWidget *l1 = gtk_label_new("Громкость аналогового усилителя (DAC PA Volume 0–40):");
    gtk_widget_set_halign(l1, GTK_ALIGN_START);
    GtkWidget *s_pa = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 40, 1);
    gtk_range_set_value(GTK_RANGE(s_pa), 28);
    g_signal_connect(s_pa, "value-changed", G_CALLBACK(on_pa_vol_changed), NULL);

    GtkWidget *l2 = gtk_label_new("Цифровое усиление левого/правого каналов (DAC FL/FR Gain 0–190):");
    gtk_widget_set_halign(l2, GTK_ALIGN_START);
    GtkWidget *s_dac = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 190, 2);
    gtk_range_set_value(GTK_RANGE(s_dac), 185);
    g_signal_connect(s_dac, "value-changed", G_CALLBACK(on_dac_gain_changed), NULL);

    GtkWidget *chk_spk = gtk_check_button_new_with_label("Включить встроенный динамик приставки (Speaker Switch)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_spk), TRUE);
    g_signal_connect(chk_spk, "toggled", G_CALLBACK(on_spk_toggled), NULL);

    gtk_box_pack_start(GTK_BOX(vb_vol), l1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb_vol), s_pa, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb_vol), l2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb_vol), s_dac, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb_vol), chk_spk, FALSE, FALSE, 4);
    gtk_container_add(GTK_CONTAINER(fr_vol), vb_vol);
    gtk_box_pack_start(GTK_BOX(vbox), fr_vol, FALSE, FALSE, 0);

    GtkWidget *fr_out = gtk_frame_new("Выбор аудиовыхода и проверка звука");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    GtkWidget *b_i2s  = gtk_button_new_with_label("Вывод: Динамик / Наушники 3.5 мм (I2S)");
    GtkWidget *b_hdmi = gtk_button_new_with_label("Вывод: Звук по кабелю HDMI");
    GtkWidget *t_spk  = gtk_button_new_with_label("▶ Тест звука (Динамик приставки)");
    GtkWidget *t_hdmi = gtk_button_new_with_label("▶ Тест звука (Монитор / ТВ HDMI)");

    GtkWidget *btns[] = {b_i2s, b_hdmi, t_spk, t_hdmi};
    for (int i = 0; i < 4; i++) {
        gtk_widget_set_hexpand(btns[i], TRUE);
        gtk_widget_set_size_request(btns[i], -1, 42);
    }
    g_signal_connect(b_i2s,  "clicked", G_CALLBACK(on_audio_route), "i2s");
    g_signal_connect(b_hdmi, "clicked", G_CALLBACK(on_audio_route), "hdmi");
    g_signal_connect(t_spk,  "clicked", G_CALLBACK(on_sound_test),  "0");
    g_signal_connect(t_hdmi, "clicked", G_CALLBACK(on_sound_test),  "1");

    gtk_grid_attach(GTK_GRID(grid), b_i2s,  0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), b_hdmi, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), t_spk,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), t_hdmi, 1, 1, 1, 1);
    gtk_container_add(GTK_CONTAINER(fr_out), grid);
    gtk_box_pack_start(GTK_BOX(vbox), fr_out, FALSE, FALSE, 0);

    return vbox;
}

/* ==================== TAB 3: WI-FI & NETWORK + GAMEPAD OSK ==================== */

static void refresh_wifi_status(void) {
    char *out = run_host_cmd(
        "if [ -d /sys/class/net/wlan0 ]; then "
        "IP=$(ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}'); "
        "SSID=$(/usr/sbin/iw dev wlan0 link 2>/dev/null | awk -F': ' '/SSID:/{print $2}'); "
        "echo \"wlan0 активен | Сеть: ${SSID:-не подключена} | IP: ${IP:-нет}\"; "
        "else echo 'Wi-Fi адаптер выключен (нажмите «Включить Wi-Fi и поиск сетей»)'; fi");
    char markup[512];
    snprintf(markup, sizeof(markup), "<b>Статус Wi-Fi:</b> %s", out);
    gtk_label_set_markup(GTK_LABEL(lbl_wifi_status), markup);
    free(out);
}

static void on_ssid_pick(GtkWidget *btn, gpointer data) {
    (void)btn;
    const char *ssid = (const char *)data;
    gtk_entry_set_text(GTK_ENTRY(entry_ssid), ssid);
    gtk_widget_grab_focus(entry_pass);
}

static void on_wifi_scan(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    gtk_label_set_markup(GTK_LABEL(lbl_wifi_status), "<b>Статус Wi-Fi:</b> Включение питания модуля и сканирование эфира...");
    while (gtk_events_pending()) gtk_main_iteration();

    GList *children = gtk_container_get_children(GTK_CONTAINER(wifi_list_box));
    for (GList *l = children; l != NULL; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    char *scan = run_host_cmd(
        "/customer/app/axp_test wifion >/dev/null 2>&1; "
        "[ -f /lib/rt2800usb.ko ] && insmod /lib/rt2800usb.ko 2>/dev/null; "
        "for i in 1 2 3 4 5 6 7 8 9 10; do [ -d /sys/class/net/wlan0 ] && break; usleep 250000; done; "
        "ip link set wlan0 up 2>/dev/null; "
        "/usr/sbin/iw dev wlan0 scan 2>/dev/null | awk '/signal:/{sig=$2} /SSID:/{sub(/^.*SSID: /,\"\"); if(length($0)>0) print $0 \"\t\" sig}' | sort -u | head -n 20");

    char *saveptr = NULL;
    int count = 0;
    for (char *line = strtok_r(scan, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        char *tab = strchr(line, '\t');
        const char *sig = "";
        if (tab) { *tab = '\0'; sig = tab + 1; }
        if (!line[0]) continue;
        char lbl[256];
        snprintf(lbl, sizeof(lbl), "≈  %s   (%s dBm)", line, sig[0] ? sig : "?");
        GtkWidget *b = gtk_button_new_with_label(lbl);
        gtk_widget_set_halign(b, GTK_ALIGN_FILL);
        char *ssid_dup = g_strdup(line);
        g_signal_connect_data(b, "clicked", G_CALLBACK(on_ssid_pick), ssid_dup, (GClosureNotify)g_free, 0);
        gtk_box_pack_start(GTK_BOX(wifi_list_box), b, FALSE, FALSE, 2);
        count++;
    }
    if (count == 0) {
        GtkWidget *empty = gtk_label_new("Сети пока не найдены — нажмите «Включить Wi-Fi и поиск сетей» ещё раз через 2 сек.");
        gtk_box_pack_start(GTK_BOX(wifi_list_box), empty, FALSE, FALSE, 4);
    }
    gtk_widget_show_all(wifi_list_box);
    free(scan);
    refresh_wifi_status();
}

static void sanitize_shell_str(const char *in, char *out, size_t sz) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < sz; i++) {
        if (in[i] == '\'' || in[i] == '\\' || in[i] == '"' || in[i] == '`' || in[i] == '$') continue;
        out[j++] = in[i];
    }
    out[j] = '\0';
}

static void on_wifi_connect(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    char ssid_s[128], pass_s[128];
    sanitize_shell_str(gtk_entry_get_text(GTK_ENTRY(entry_ssid)), ssid_s, sizeof(ssid_s));
    sanitize_shell_str(gtk_entry_get_text(GTK_ENTRY(entry_pass)), pass_s, sizeof(pass_s));
    if (!ssid_s[0]) return;

    gtk_label_set_markup(GTK_LABEL(lbl_wifi_status), "<b>Статус Wi-Fi:</b> Подключение и получение IP по DHCP...");
    while (gtk_events_pending()) gtk_main_iteration();

    FILE *wf = fopen("/mnt/sd/wifi.txt", "w");
    if (wf) {
        fprintf(wf, "%s\n%s\n", ssid_s, pass_s);
        fclose(wf);
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "/bin/wifi-connect \"%s\" \"%s\"", ssid_s, pass_s);
    char *res = run_host_cmd(cmd);
    free(res);
    sleep(1);
    refresh_wifi_status();
}

static void on_show_pass_toggled(GtkToggleButton *btn, gpointer data) {
    (void)data;
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), gtk_toggle_button_get_active(btn));
}

static void on_osk_key(GtkWidget *w, gpointer data) {
    (void)w;
    const char *k = (const char *)data;
    GtkWidget *target = gtk_widget_has_focus(entry_ssid) ? entry_ssid : entry_pass;
    const char *cur = gtk_entry_get_text(GTK_ENTRY(target));
    if (strcmp(k, "BKSP") == 0) {
        size_t len = strlen(cur);
        if (len > 0) {
            char *tmp = g_strndup(cur, len - 1);
            gtk_entry_set_text(GTK_ENTRY(target), tmp);
            g_free(tmp);
        }
        return;
    }
    if (strcmp(k, "CLR") == 0) {
        gtk_entry_set_text(GTK_ENTRY(target), "");
        return;
    }
    if (strcmp(k, "SHIFT") == 0) {
        osk_shift = !osk_shift;
        return;
    }
    char ch[2] = { k[0], '\0' };
    if (osk_shift && ch[0] >= 'a' && ch[0] <= 'z') ch[0] = (char)(ch[0] - 'a' + 'A');
    char *next = g_strconcat(cur, ch, NULL);
    gtk_entry_set_text(GTK_ENTRY(target), next);
    g_free(next);
}

static void on_toggle_osk(GtkToggleButton *btn, gpointer data) {
    (void)data;
    if (gtk_toggle_button_get_active(btn)) gtk_widget_show_all(osk_frame);
    else gtk_widget_hide(osk_frame);
}

static GtkWidget *build_wifi_tab(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    lbl_wifi_status = gtk_label_new("");
    gtk_widget_set_halign(lbl_wifi_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_wifi_status, FALSE, FALSE, 0);

    GtkWidget *hb_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *b_scan = gtk_button_new_with_label("↻ Включить Wi-Fi и найти сети");
    GtkWidget *b_osk  = gtk_toggle_button_new_with_label("⌨ Экранная клавиатура (для геймпада)");
    gtk_widget_set_size_request(b_scan, -1, 38);
    g_signal_connect(b_scan, "clicked", G_CALLBACK(on_wifi_scan), NULL);
    g_signal_connect(b_osk,  "toggled", G_CALLBACK(on_toggle_osk), NULL);
    gtk_box_pack_start(GTK_BOX(hb_top), b_scan, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hb_top), b_osk,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hb_top, FALSE, FALSE, 0);

    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sc, -1, 120);
    wifi_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(sc), wifi_list_box);
    gtk_box_pack_start(GTK_BOX(vbox), sc, TRUE, TRUE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    entry_ssid = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_ssid), "Имя сети (SSID)");
    gtk_widget_set_hexpand(entry_ssid, TRUE);

    entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "Пароль WPA2");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    gtk_widget_set_hexpand(entry_pass, TRUE);

    GtkWidget *chk_vis = gtk_check_button_new_with_label("Показать");
    g_signal_connect(chk_vis, "toggled", G_CALLBACK(on_show_pass_toggled), NULL);

    GtkWidget *b_conn = gtk_button_new_with_label("✔ Подключиться и сохранить на SD");
    gtk_widget_set_size_request(b_conn, -1, 38);
    g_signal_connect(b_conn, "clicked", G_CALLBACK(on_wifi_connect), NULL);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Сеть SSID:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_ssid,                  1, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Пароль:"),    0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_pass,                  1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chk_vis,                     2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), b_conn,                      0, 2, 3, 1);
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    /* Compact On-Screen Keyboard for Gamepad / Mouse entry */
    osk_frame = gtk_frame_new("Экранная клавиатура (SHIFT переключает регистр)");
    GtkWidget *osk_vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_set_border_width(GTK_CONTAINER(osk_vb), 6);
    const char *rows[] = {
        "1234567890-_@.",
        "qwertyuiop!#$%",
        "asdfghjklzxcvbnm"
    };
    for (int r = 0; r < 3; r++) {
        GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        for (size_t i = 0; rows[r][i]; i++) {
            char s[2] = { rows[r][i], '\0' };
            GtkWidget *kb = gtk_button_new_with_label(s);
            gtk_button_set_focus_on_click(GTK_BUTTON(kb), FALSE);
            g_signal_connect(kb, "clicked", G_CALLBACK(on_osk_key), g_strdup(s));
            gtk_box_pack_start(GTK_BOX(hb), kb, TRUE, TRUE, 0);
        }
        if (r == 2) {
            GtkWidget *b_sh = gtk_button_new_with_label("SHIFT");
            GtkWidget *b_bk = gtk_button_new_with_label("⌫");
            GtkWidget *b_cl = gtk_button_new_with_label("Очистить");
            gtk_button_set_focus_on_click(GTK_BUTTON(b_sh), FALSE);
            gtk_button_set_focus_on_click(GTK_BUTTON(b_bk), FALSE);
            gtk_button_set_focus_on_click(GTK_BUTTON(b_cl), FALSE);
            g_signal_connect(b_sh, "clicked", G_CALLBACK(on_osk_key), "SHIFT");
            g_signal_connect(b_bk, "clicked", G_CALLBACK(on_osk_key), "BKSP");
            g_signal_connect(b_cl, "clicked", G_CALLBACK(on_osk_key), "CLR");
            gtk_box_pack_start(GTK_BOX(hb), b_sh, FALSE, FALSE, 2);
            gtk_box_pack_start(GTK_BOX(hb), b_bk, FALSE, FALSE, 2);
            gtk_box_pack_start(GTK_BOX(hb), b_cl, FALSE, FALSE, 2);
        }
        gtk_box_pack_start(GTK_BOX(osk_vb), hb, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(osk_frame), osk_vb);
    gtk_box_pack_start(GTK_BOX(vbox), osk_frame, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(osk_frame, TRUE);

    refresh_wifi_status();
    return vbox;
}

/* ==================== TAB 4: CPU, 256MB RAM & BATTERY ==================== */

#include <fcntl.h>
#include <sys/mman.h>

static int read_true_cpu_mhz(void) {
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd >= 0) {
        void *map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0xb0150000);
        close(fd);
        if (map != MAP_FAILED) {
            unsigned int pll = *(volatile unsigned int *)map;
            munmap(map, 4096);
            int mult = (int)(pll & 0x7FU);
            if (mult >= 10 && mult <= 120) return mult * 12;
        }
    }
    return read_int_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", 900000) / 1000;
}

static gboolean refresh_sys_telemetry(gpointer data) {
    (void)data;
    if (!lbl_sys_status || !gtk_widget_get_realized(lbl_sys_status)) return TRUE;

    char prof[32], bat_stat[32];
    read_str_file("/tmp/cpu_profile", prof, sizeof(prof), "stock");
    read_str_file("/sys/class/power_supply/battery/status", bat_stat, sizeof(bat_stat), "Unknown");
    int mhz = read_true_cpu_mhz();
    int cap = read_int_file("/sys/class/power_supply/battery/capacity", 100);
    int uvolts = read_int_file("/sys/class/power_supply/battery/voltage_now", 4000000);
    int usb_on = read_int_file("/sys/class/power_supply/atc260x-usb/online", 0);

    long mem_total = 0, mem_free = 0, mem_buf = 0, mem_cached = 0, swap_total = 0, swap_free = 0;
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[128];
        while (fgets(line, sizeof(line), mf)) {
            sscanf(line, "MemTotal: %ld kB", &mem_total);
            sscanf(line, "MemFree: %ld kB", &mem_free);
            sscanf(line, "Buffers: %ld kB", &mem_buf);
            sscanf(line, "Cached: %ld kB", &mem_cached);
            sscanf(line, "SwapTotal: %ld kB", &swap_total);
            sscanf(line, "SwapFree: %ld kB", &swap_free);
        }
        fclose(mf);
    }
    long mem_avail = mem_free + mem_buf + mem_cached;
    long mem_used = (mem_total > mem_avail) ? (mem_total - mem_avail) : 0;

    char txt[1024];
    snprintf(txt, sizeof(txt),
             "<b>Процессор ATM7051H:</b> %d МГц  (профиль: <b>%s</b>)\n"
             "<b>ОЗУ DDR3 (256 МБ):</b> занято <b>%ld МБ</b>, свободно <b>%ld МБ</b> из %ld МБ   |   <b>Swap:</b> %ld / %ld МБ\n"
             "<b>Питание и Батарея:</b> %d%% (%.2f В, %s)   |   <b>USB питание:</b> %s",
             mhz, prof,
             mem_used / 1024, mem_avail / 1024, mem_total / 1024,
             (swap_total - swap_free) / 1024, swap_total / 1024,
             cap, uvolts / 1000000.0, bat_stat,
             usb_on ? "подключено" : "автономно");
    gtk_label_set_markup(GTK_LABEL(lbl_sys_status), txt);
    return TRUE;
}

static void on_cpu_profile(GtkWidget *w, gpointer data) {
    (void)w;
    const char *prof = (const char *)data;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "/bin/perf-mode %s", prof);
    char *out = run_host_cmd(cmd);
    free(out);
    refresh_sys_telemetry(NULL);
}

static void on_exit_desktop(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    unlink("/tmp/desktop_relaunch");
    unlink("/tmp/desktop_dualscreen");
    int r = system("killall jwm openbox 2>/dev/null");
    (void)r;
}

static void on_reboot_console(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    run_host_bg("sync; sleep 1; reboot -f");
}

static GtkWidget *build_sys_tab(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 14);

    lbl_sys_status = gtk_label_new("");
    gtk_widget_set_halign(lbl_sys_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_sys_status, FALSE, FALSE, 4);

    GtkWidget *fr_cpu = gtk_frame_new("Профили частоты и энергопотребления процессора (2 × Cortex-A5)");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    struct { const char *lbl; const char *mode; int c; int r; } profs[] = {
        {"⚡ Smart (240–1260 МГц авто + UV)", "smart",    0, 0},
        {"⚙ Stock (900 МГц штатный)",         "stock",    1, 0},
        {"♦ Eco (240 МГц макс. батарея)",     "eco",      0, 1},
        {"≈ Balanced (600 МГц тихий)",        "balanced", 1, 1},
        {"▲ Turbo (1200 МГц разгон)",         "turbo",    0, 2},
        {"★ Max (1272 МГц максимум)",         "max",      1, 2},
    };
    for (int i = 0; i < 6; i++) {
        GtkWidget *b = gtk_button_new_with_label(profs[i].lbl);
        gtk_widget_set_hexpand(b, TRUE);
        gtk_widget_set_size_request(b, -1, 40);
        g_signal_connect(b, "clicked", G_CALLBACK(on_cpu_profile), (gpointer)profs[i].mode);
        gtk_grid_attach(GTK_GRID(grid), b, profs[i].c, profs[i].r, 1, 1);
    }
    gtk_container_add(GTK_CONTAINER(fr_cpu), grid);
    gtk_box_pack_start(GTK_BOX(vbox), fr_cpu, FALSE, FALSE, 0);

    GtkWidget *hb_pwr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *b_exit = gtk_button_new_with_label("⬅ Выйти из рабочего стола в консоль на 3.5\" экране");
    GtkWidget *b_reb  = gtk_button_new_with_label("↻ Перезагрузить приставку");
    gtk_widget_set_size_request(b_exit, -1, 40);
    gtk_widget_set_size_request(b_reb, -1, 40);
    g_signal_connect(b_exit, "clicked", G_CALLBACK(on_exit_desktop), NULL);
    g_signal_connect(b_reb,  "clicked", G_CALLBACK(on_reboot_console), NULL);
    gtk_box_pack_start(GTK_BOX(hb_pwr), b_exit, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hb_pwr), b_reb,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hb_pwr, FALSE, FALSE, 4);

    g_timeout_add(2000, refresh_sys_telemetry, NULL);
    return vbox;
}

int main(int argc, char **argv) {
    /* Single-instance guard so clicking launcher replaces cleanly instead of stacking copies */
    int old_pid = read_int_file("/tmp/xgb-settings.pid", 0);
    if (old_pid > 1 && old_pid != (int)getpid()) {
        char proc_p[64];
        snprintf(proc_p, sizeof(proc_p), "/proc/%d/cmdline", old_pid);
        if (access(proc_p, R_OK) == 0) {
            char kcmd[64];
            snprintf(kcmd, sizeof(kcmd), "kill %d 2>/dev/null", old_pid);
            int r = system(kcmd);
            (void)r;
            usleep(80000);
        }
    }
    FILE *pf = fopen("/tmp/xgb-settings.pid", "w");
    if (pf) { fprintf(pf, "%d\n", (int)getpid()); fclose(pf); }

    gtk_init(&argc, &argv);

    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings,
                     "gtk-application-prefer-dark-theme", TRUE,
                     "gtk-font-name", "DejaVu Sans 11",
                     "gtk-icon-theme-name", "gnome-brave",
                     NULL);
    }

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Центр настроек системы XGB35");
    gtk_window_set_default_size(GTK_WINDOW(win), 780, 500);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *nb = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_display_tab(), gtk_label_new("▣  Экран и Мультиэкран"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_audio_tab(),   gtk_label_new("♪  Звук и Громкость"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_wifi_tab(),    gtk_label_new("≈  Wi-Fi и Сеть"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), build_sys_tab(),     gtk_label_new("⚡  Процессор, ОЗУ и Питание"));

    if (argc > 1) {
        int tab = atoi(argv[1]);
        if (tab >= 0 && tab <= 3) gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), tab);
    }

    gtk_container_add(GTK_CONTAINER(win), nb);
    gtk_widget_show_all(win);
    refresh_sys_telemetry(NULL);
    gtk_main();
    return 0;
}
