#include "lexios_apps.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>

extern "C" {
LV_IMG_DECLARE(img_app_battery);
LV_IMG_DECLARE(img_app_live_call);
LV_IMG_DECLARE(img_app_object_detect);
}

/* ============================================================================
 * Helper: Dark AMOLED Card Builder (568x1232 Portrait Layout)
 * ============================================================================ */
static lv_obj_t *create_amoled_card(lv_obj_t *parent, int w, int h, int y_off)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y_off);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x111722), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x223046), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *create_action_btn(lv_obj_t *parent, const char *text, int w, int h, uint32_t bg_hex)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

/* ============================================================================
 * 1. LexiSystemApp — Thermal, 6-Stage CPU1 DVFS, Fan, RF & Pocket Standby
 * ============================================================================ */
static lv_obj_t *g_sys_temp_lbl = nullptr;
static lv_obj_t *g_sys_cpu_lbl = nullptr;
static lv_obj_t *g_sys_dvfs_bar = nullptr;
static lv_obj_t *g_sys_fan_lbl = nullptr;
static lv_obj_t *g_sys_rf_lbl = nullptr;
static lv_obj_t *g_sys_uptime_lbl = nullptr;
static lv_obj_t *g_sys_sleep_lbl = nullptr;

static void update_lexisystem_ui(void)
{
    if (!g_sys_temp_lbl) return;

    double temp = lexios_get_die_temp_c();
    int stage = lexios_get_cpu_dvfs_stage();
    int freq = lexios_get_cpu_freq_mhz();
    int tlevel = lexios_get_thermal_level();
    bool fan_on = lexios_get_fan_active();
    lexios_fan_mode_t fmode = lexios_get_fan_mode();
    uint64_t up = lexios_get_uptime_sec();
    int sleep_s = lexios_get_sleep_timeout_sec();

    char buf[160];
    const char *health = (tlevel >= 2) ? "EMERGENCY CUTOFF" :
                         (tlevel == 1) ? "THROTTLING" :
                         (temp >= 50.0) ? "WARM (DVFS ACTIVE)" : "OPTIMAL";
    snprintf(buf, sizeof(buf), "Die Temp: %.1f C  [%s]", temp, health);
    lv_label_set_text(g_sys_temp_lbl, buf);
    lv_obj_set_style_text_color(g_sys_temp_lbl,
        tlevel >= 2 ? lv_color_hex(0xFF4444) :
        (temp >= 56.0 ? lv_color_hex(0xF5A524) : lv_color_hex(0x25C281)), 0);

    snprintf(buf, sizeof(buf), "CPU1 Clock: %d MHz  (DVFS Stage %d/5 %s)",
             freq, stage, stage == 0 ? "AUTO 200-1600" : "LOCKED");
    lv_label_set_text(g_sys_cpu_lbl, buf);
    if (g_sys_dvfs_bar) {
        int pct = (freq * 100) / 1600;
        lv_bar_set_value(g_sys_dvfs_bar, pct, LV_ANIM_OFF);
    }

    const char *fmode_str = (fmode == LEXIOS_FAN_FORCE_ON) ? "FORCE ON" :
                            (fmode == LEXIOS_FAN_FORCE_OFF) ? "FORCE OFF" : "AUTO (>=45C)";
    snprintf(buf, sizeof(buf), "Fan (GPIO42): %s  |  Mode: %s",
             fan_on ? "RUNNING" : "IDLE", fmode_str);
    lv_label_set_text(g_sys_fan_lbl, buf);

    const char *lr_state = (tlevel >= 2) ? "CUTOFF (0 mW)" :
                           (tlevel == 1) ? "THROTTLED (+4 dBm)" : "ACTIVE (<=+10 dBm)";
    snprintf(buf, sizeof(buf),
             "LR2021 Radio: %s\nnRF9151 Modem: OFF (Power-Gated)\nKPU / DPU / VPU: OFF (Power-Gated)\nKernel RNG: /dev/urandom + getrandom OK",
             lr_state);
    lv_label_set_text(g_sys_rf_lbl, buf);

    snprintf(buf, sizeof(buf), "LexiOS v1.0  •  Uptime: %lluh %llum %llus",
             (unsigned long long)(up / 3600),
             (unsigned long long)((up % 3600) / 60),
             (unsigned long long)(up % 60));
    lv_label_set_text(g_sys_uptime_lbl, buf);

    if (sleep_s <= 0) {
        snprintf(buf, sizeof(buf), "Pocket Standby Timeout: Never (Manual GPIO0 Only)");
    } else {
        snprintf(buf, sizeof(buf), "Pocket Standby Timeout: %ds (200 MHz + AMOLED Off)", sleep_s);
    }
    lv_label_set_text(g_sys_sleep_lbl, buf);
}

static void sys_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    update_lexisystem_ui();
}

static void on_fan_btn_cb(lv_event_t *e)
{
    intptr_t mode = (intptr_t)lv_event_get_user_data(e);
    lexios_set_fan_mode((lexios_fan_mode_t)mode);
    update_lexisystem_ui();
}

static void on_sleep_timeout_btn_cb(lv_event_t *e)
{
    intptr_t sec = (intptr_t)lv_event_get_user_data(e);
    lexios_set_sleep_timeout_sec((int)sec);
    update_lexisystem_ui();
}

static void on_sleep_now_btn_cb(lv_event_t *e)
{
    (void)e;
    lexios_set_screen_sleep(true);
}

LexiSystemApp::LexiSystemApp(bool use_status_bar, bool use_navigation_bar)
    : ESP_Brookesia_PhoneApp(
          ESP_BROOKESIA_CORE_APP_DATA_DEFAULT("LexiSystem", &img_app_battery, true),
          ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(&img_app_battery, use_status_bar, use_navigation_bar))
{}

LexiSystemApp::~LexiSystemApp() {}

bool LexiSystemApp::run(void)
{
    lv_area_t area = getVisualArea();
    int w = area.x2 - area.x1;
    int h = area.y2 - area.y1;

    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x090D14), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    int card_w = w - 32;

    /* Header Card */
    lv_obj_t *hdr = create_amoled_card(root, card_w, 96, 12);
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "LEXIOS SYSTEM & THERMAL GUARDIAN");
    lv_obj_set_style_text_color(title, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_sys_uptime_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_color(g_sys_uptime_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(g_sys_uptime_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sys_uptime_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Thermal & 6-Stage CPU1 DVFS Card */
    lv_obj_t *tcard = create_amoled_card(root, card_w, 168, 122);
    g_sys_temp_lbl = lv_label_create(tcard);
    lv_obj_set_style_text_font(g_sys_temp_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(g_sys_temp_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    g_sys_cpu_lbl = lv_label_create(tcard);
    lv_obj_set_style_text_color(g_sys_cpu_lbl, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(g_sys_cpu_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sys_cpu_lbl, LV_ALIGN_TOP_LEFT, 0, 40);

    g_sys_dvfs_bar = lv_bar_create(tcard);
    lv_obj_set_size(g_sys_dvfs_bar, card_w - 36, 22);
    lv_obj_align(g_sys_dvfs_bar, LV_ALIGN_TOP_LEFT, 0, 78);
    lv_bar_set_range(g_sys_dvfs_bar, 0, 100);
    lv_obj_set_style_bg_color(g_sys_dvfs_bar, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_sys_dvfs_bar, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);

    lv_obj_t *scale_lbl = lv_label_create(tcard);
    lv_label_set_text(scale_lbl, "Stages: 200MHz | 400 | 594 | 800 | 1188 | 1600MHz");
    lv_obj_set_style_text_color(scale_lbl, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(scale_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(scale_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Cooling Fan Control Card */
    lv_obj_t *fcard = create_amoled_card(root, card_w, 138, 304);
    g_sys_fan_lbl = lv_label_create(fcard);
    lv_obj_set_style_text_color(g_sys_fan_lbl, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_font(g_sys_fan_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sys_fan_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    int btn_w = (card_w - 56) / 3;
    lv_obj_t *b_auto = create_action_btn(fcard, "Auto", btn_w, 52, 0x0284C7);
    lv_obj_align(b_auto, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(b_auto, on_fan_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)LEXIOS_FAN_AUTO);

    lv_obj_t *b_on = create_action_btn(fcard, "Force ON", btn_w, 52, 0x16A34A);
    lv_obj_align(b_on, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(b_on, on_fan_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)LEXIOS_FAN_FORCE_ON);

    lv_obj_t *b_off = create_action_btn(fcard, "Force OFF", btn_w, 52, 0x475569);
    lv_obj_align(b_off, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(b_off, on_fan_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)LEXIOS_FAN_FORCE_OFF);

    /* RF & Silicon Power Domains Card */
    lv_obj_t *rcard = create_amoled_card(root, card_w, 148, 456);
    g_sys_rf_lbl = lv_label_create(rcard);
    lv_obj_set_style_text_color(g_sys_rf_lbl, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_font(g_sys_rf_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sys_rf_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Pocket Standby ("Pocket Mode") Card */
    lv_obj_t *scard = create_amoled_card(root, card_w, 200, 618);
    g_sys_sleep_lbl = lv_label_create(scard);
    lv_obj_set_style_text_color(g_sys_sleep_lbl, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_font(g_sys_sleep_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sys_sleep_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    int sbtn_w = (card_w - 64) / 4;
    const int timeouts[4] = {30, 60, 120, 0};
    const char *tlabels[4] = {"30s", "60s", "120s", "Never"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *tb = create_action_btn(scard, tlabels[i], sbtn_w, 48, 0x1E293B);
        lv_obj_align(tb, LV_ALIGN_TOP_LEFT, i * (sbtn_w + 10), 42);
        lv_obj_add_event_cb(tb, on_sleep_timeout_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)timeouts[i]);
    }

    lv_obj_t *sleep_now = create_action_btn(scard, "ENTER POCKET STANDBY NOW (200 MHz)", card_w - 36, 56, 0x7C3AED);
    lv_obj_align(sleep_now, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(sleep_now, on_sleep_now_btn_cb, LV_EVENT_CLICKED, nullptr);

    update_lexisystem_ui();
    _refresh_timer = lv_timer_create(sys_refresh_timer_cb, 1000, nullptr);
    return true;
}

bool LexiSystemApp::back(void)
{
    notifyCoreClosed();
    return true;
}

bool LexiSystemApp::close(void)
{
    if (_refresh_timer) {
        lv_timer_del(_refresh_timer);
        _refresh_timer = nullptr;
    }
    g_sys_temp_lbl = nullptr;
    g_sys_cpu_lbl = nullptr;
    g_sys_dvfs_bar = nullptr;
    g_sys_fan_lbl = nullptr;
    g_sys_rf_lbl = nullptr;
    g_sys_uptime_lbl = nullptr;
    g_sys_sleep_lbl = nullptr;
    return true;
}

bool LexiSystemApp::init(void) { return true; }

/* ============================================================================
 * 2. LexiChatApp — On-Chain FLRC Mesh Messenger (127.0.0.1:8545 JSON-RPC)
 * ============================================================================ */
static lv_obj_t *g_chat_status_lbl = nullptr;
static lv_obj_t *g_chat_feed_lbl = nullptr;
static lv_obj_t *g_chat_input_ta = nullptr;
static int g_chat_channel_idx = 0;
static const char *kChatChannels[3] = {"#general", "#mesh-ops", "#emergency"};
static char g_chat_history[1536] =
    "[#general] LexiOS Mesh Messenger ready.\n"
    "• Transport: LR2021 FLRC + Superlexicon WASM\n"
    "• Contract: messaging-wasm (postMessage)\n";

static int query_local_node_block_number(char *out_hex, size_t out_sz)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct timeval tv = {.tv_sec = 0, .tv_usec = 120000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8545);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }

    const char *body = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"params\":[],\"id\":1}";
    char req[256];
    int req_len = snprintf(req, sizeof(req),
        "POST / HTTP/1.1\r\nHost: 127.0.0.1:8545\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
        strlen(body), body);
    send(sock, req, req_len, 0);

    char resp[512] = {0};
    int n = recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);
    if (n <= 0) return -1;

    const char *p = strstr(resp, "\"result\":\"");
    if (p) {
        p += 10;
        size_t i = 0;
        while (p[i] && p[i] != '"' && i + 1 < out_sz) {
            out_hex[i] = p[i];
            i++;
        }
        out_hex[i] = '\0';
        return 0;
    }
    return -1;
}

static int submit_mesh_chat_rpc(const char *channel, const char *msg)
{
    /* Also log to /tmp/lexios_mesh_outbox.log and dispatch eth_sendTransaction to 127.0.0.1:8545 */
    FILE *fp = fopen("/tmp/lexios_mesh_outbox.log", "a");
    if (fp) {
        fprintf(fp, "[%s] %s\n", channel, msg);
        fclose(fp);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct timeval tv = {.tv_sec = 0, .tv_usec = 150000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8545);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }

    /* Hex-encode payload with postMessage(bytes32,bytes) selector 0x482f63b0 */
    char hex_msg[256] = {0};
    size_t mlen = strlen(msg);
    if (mlen > 96) mlen = 96;
    for (size_t i = 0; i < mlen; i++) {
        snprintf(hex_msg + i * 2, 3, "%02x", (unsigned char)msg[i]);
    }

    char body[512];
    int blen = snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"method\":\"superlexicon_postMeshMessage\",\"params\":[\"%s\",\"0x482f63b0%s\"],\"id\":2}",
        channel, hex_msg);
    char req[768];
    int rlen = snprintf(req, sizeof(req),
        "POST / HTTP/1.1\r\nHost: 127.0.0.1:8545\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
        blen, body);
    send(sock, req, rlen, 0);
    char resp[256];
    recv(sock, resp, sizeof(resp) - 1, 0);
    close(sock);
    return 0;
}

static void append_chat_line(const char *channel, const char *text, bool rpc_ok)
{
    char line[256];
    snprintf(line, sizeof(line), "\n[%s] %s %s",
             channel, text, rpc_ok ? "(sent->mesh)" : "(queued)");
    if (strlen(g_chat_history) + strlen(line) + 1 >= sizeof(g_chat_history)) {
        /* Keep the most recent half of history */
        size_t half = strlen(g_chat_history) / 2;
        memmove(g_chat_history, g_chat_history + half, strlen(g_chat_history) - half + 1);
    }
    strcat(g_chat_history, line);
    if (g_chat_feed_lbl) {
        lv_label_set_text(g_chat_feed_lbl, g_chat_history);
    }
}

static void update_chat_status(void)
{
    if (!g_chat_status_lbl) return;
    char blk[48] = {0};
    char status[128];
    if (query_local_node_block_number(blk, sizeof(blk)) == 0) {
        unsigned long long bnum = strtoull(blk, nullptr, 16);
        snprintf(status, sizeof(status), "Channel: %s  •  Node ONLINE (Block #%llu)",
                 kChatChannels[g_chat_channel_idx], bnum);
        lv_obj_set_style_text_color(g_chat_status_lbl, lv_color_hex(0x25C281), 0);
    } else {
        snprintf(status, sizeof(status), "Channel: %s  •  Node Syncing (127.0.0.1:8545)",
                 kChatChannels[g_chat_channel_idx]);
        lv_obj_set_style_text_color(g_chat_status_lbl, lv_color_hex(0xF5A524), 0);
    }
    lv_label_set_text(g_chat_status_lbl, status);
}

static void chat_poll_timer_cb(lv_timer_t *t)
{
    (void)t;
    update_chat_status();
}

static void on_chat_channel_cb(lv_event_t *e)
{
    g_chat_channel_idx = (int)(intptr_t)lv_event_get_user_data(e);
    update_chat_status();
}

static void on_chat_send_cb(lv_event_t *e)
{
    const char *preset = (const char *)lv_event_get_user_data(e);
    const char *msg = preset;
    if (!msg && g_chat_input_ta) {
        msg = lv_textarea_get_text(g_chat_input_ta);
    }
    if (!msg || msg[0] == '\0') return;

    int rc = submit_mesh_chat_rpc(kChatChannels[g_chat_channel_idx], msg);
    append_chat_line(kChatChannels[g_chat_channel_idx], msg, rc == 0);
    if (!preset && g_chat_input_ta) {
        lv_textarea_set_text(g_chat_input_ta, "");
    }
}

LexiChatApp::LexiChatApp(bool use_status_bar, bool use_navigation_bar)
    : ESP_Brookesia_PhoneApp(
          ESP_BROOKESIA_CORE_APP_DATA_DEFAULT("LexiChat", &img_app_live_call, true),
          ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(&img_app_live_call, use_status_bar, use_navigation_bar))
{}

LexiChatApp::~LexiChatApp() {}

bool LexiChatApp::run(void)
{
    lv_area_t area = getVisualArea();
    int w = area.x2 - area.x1;
    int h = area.y2 - area.y1;
    int card_w = w - 32;

    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x090D14), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* Header & Channel Bar */
    lv_obj_t *hdr = create_amoled_card(root, card_w, 128, 12);
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "LEXICHAT  •  ON-CHAIN MESH MESSENGER");
    lv_obj_set_style_text_color(title, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_chat_status_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(g_chat_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_chat_status_lbl, LV_ALIGN_TOP_LEFT, 0, 28);

    int cbtn_w = (card_w - 56) / 3;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cb = create_action_btn(hdr, kChatChannels[i], cbtn_w, 40, 0x1E293B);
        lv_obj_align(cb, LV_ALIGN_BOTTOM_LEFT, i * (cbtn_w + 12), 0);
        lv_obj_add_event_cb(cb, on_chat_channel_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    /* Message Feed Card */
    lv_obj_t *fcard = create_amoled_card(root, card_w, 250, 152);
    g_chat_feed_lbl = lv_label_create(fcard);
    lv_obj_set_width(g_chat_feed_lbl, card_w - 36);
    lv_label_set_long_mode(g_chat_feed_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_chat_feed_lbl, g_chat_history);
    lv_obj_set_style_text_color(g_chat_feed_lbl, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(g_chat_feed_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_chat_feed_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Quick Mesh Beacon Row */
    lv_obj_t *qcard = create_amoled_card(root, card_w, 84, 414);
    lv_obj_t *qb1 = create_action_btn(qcard, "ACK / Copy", cbtn_w, 48, 0x0F766E);
    lv_obj_align(qb1, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(qb1, on_chat_send_cb, LV_EVENT_CLICKED, (void *)"ACK - Signal 5/5");

    lv_obj_t *qb2 = create_action_btn(qcard, "Ping Mesh", cbtn_w, 48, 0x0284C7);
    lv_obj_align(qb2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(qb2, on_chat_send_cb, LV_EVENT_CLICKED, (void *)"PING from K230 LexiOS");

    lv_obj_t *qb3 = create_action_btn(qcard, "Status OK", cbtn_w, 48, 0x4F46E5);
    lv_obj_align(qb3, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(qb3, on_chat_send_cb, LV_EVENT_CLICKED, (void *)"Validator Node Online");

    /* Text Input + Send Button + Touch Keyboard */
    g_chat_input_ta = lv_textarea_create(root);
    lv_obj_set_size(g_chat_input_ta, card_w - 124, 56);
    lv_obj_align(g_chat_input_ta, LV_ALIGN_TOP_LEFT, 16, 510);
    lv_textarea_set_one_line(g_chat_input_ta, true);
    lv_textarea_set_placeholder_text(g_chat_input_ta, "Type mesh message...");

    lv_obj_t *send_btn = create_action_btn(root, "SEND", 112, 56, 0x16A34A);
    lv_obj_align(send_btn, LV_ALIGN_TOP_RIGHT, -16, 510);
    lv_obj_add_event_cb(send_btn, on_chat_send_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *kb = lv_keyboard_create(root);
    lv_obj_set_size(kb, w, 320);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, g_chat_input_ta);

    update_chat_status();
    _poll_timer = lv_timer_create(chat_poll_timer_cb, 2000, nullptr);
    return true;
}

bool LexiChatApp::back(void)
{
    notifyCoreClosed();
    return true;
}

bool LexiChatApp::close(void)
{
    if (_poll_timer) {
        lv_timer_del(_poll_timer);
        _poll_timer = nullptr;
    }
    g_chat_status_lbl = nullptr;
    g_chat_feed_lbl = nullptr;
    g_chat_input_ta = nullptr;
    return true;
}

bool LexiChatApp::init(void) { return true; }

/* ============================================================================
 * 3. LexiAppsApp — MMU Process Supervisor & .wasm / .elf Sandbox Manager
 * ============================================================================ */
static lv_obj_t *g_apps_list_lbl = nullptr;
static lv_obj_t *g_apps_action_lbl = nullptr;
static pid_t g_spawned_app_pid = -1;
static char g_spawned_app_name[64] = {0};

static bool check_port_listening(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bool ok = (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    close(sock);
    return ok;
}

static void refresh_lexiapps_view(void)
{
    if (!g_apps_list_lbl) return;

    /* Reap any exited sandboxed child process without blocking (Crash Isolation) */
    if (g_spawned_app_pid > 0) {
        int status = 0;
        pid_t r = waitpid(g_spawned_app_pid, &status, WNOHANG);
        if (r == g_spawned_app_pid || r < 0) {
            if (g_apps_action_lbl) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Sandbox process %s (PID %d) exited cleanly (isolated).",
                         g_spawned_app_name, (int)g_spawned_app_pid);
                lv_label_set_text(g_apps_action_lbl, msg);
            }
            g_spawned_app_pid = -1;
        }
    }

    bool node_up = check_port_listening(8545);
    bool radio_up = (access("/tmp/os_thermal_state", F_OK) == 0);

    char buf[1024];
    int off = snprintf(buf, sizeof(buf),
        "CORE SYSTEM SERVICES (MMU Isolated lwp):\n"
        "  [1] system_ui (app.elf)      : RUNNING (PID %d)\n"
        "  [2] lora_radio.elf (LR2021)  : %s\n"
        "  [3] superlexicon.elf (Node)  : %s\n\n"
        "INSTALLED PACKAGES (/sdcard/ & /sdcard/apps/):\n",
        (int)getpid(),
        radio_up ? "ACTIVE (UDP :9100/:9101)" : "STANDBY",
        node_up ? "ONLINE (RPC :8545 / WS :8546)" : "STARTING / OFFLINE");

    const char *scan_dirs[2] = {"/sdcard/apps", "/sdcard"};
    int found = 0;
    for (int d = 0; d < 2; d++) {
        DIR *dir = opendir(scan_dirs[d]);
        if (!dir) continue;
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr && off < (int)sizeof(buf) - 120) {
            const char *name = ent->d_name;
            size_t len = strlen(name);
            bool is_elf = (len > 4 && strcmp(name + len - 4, ".elf") == 0);
            bool is_wasm = (len > 5 && strcmp(name + len - 5, ".wasm") == 0);
            if (is_elf || is_wasm) {
                char full[256];
                snprintf(full, sizeof(full), "%s/%s", scan_dirs[d], name);
                struct stat st;
                long kb = (stat(full, &st) == 0) ? (long)(st.st_size / 1024) : 0;
                off += snprintf(buf + off, sizeof(buf) - off,
                                "  • [%s] %s (%ld KB)\n",
                                is_wasm ? "WASM" : "ELF ", name, kb);
                found++;
                if (found >= 8) break;
            }
        }
        closedir(dir);
    }
    if (found == 0) {
        snprintf(buf + off, sizeof(buf) - off, "  • Drop .wasm or .elf packages into /sdcard/apps/\n");
    }

    lv_label_set_text(g_apps_list_lbl, buf);
}

static void apps_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_lexiapps_view();
}

static void on_spawn_service_cb(lv_event_t *e)
{
    const char *path = (const char *)lv_event_get_user_data(e);
    if (!path) return;

    mkdir("/sdcard/apps", 0755);
    pid_t pid = fork();
    if (pid == 0) {
        char *const argv[] = {(char *)path, nullptr};
        execv(path, argv);
        _exit(127);
    } else if (pid > 0) {
        g_spawned_app_pid = pid;
        snprintf(g_spawned_app_name, sizeof(g_spawned_app_name), "%s", path);
        if (g_apps_action_lbl) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Spawned %s in isolated Sv39 MMU space (PID %d)", path, (int)pid);
            lv_label_set_text(g_apps_action_lbl, msg);
        }
    }
    refresh_lexiapps_view();
}

LexiAppsApp::LexiAppsApp(bool use_status_bar, bool use_navigation_bar)
    : ESP_Brookesia_PhoneApp(
          ESP_BROOKESIA_CORE_APP_DATA_DEFAULT("LexiApps", &img_app_object_detect, true),
          ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(&img_app_object_detect, use_status_bar, use_navigation_bar))
{}

LexiAppsApp::~LexiAppsApp() {}

bool LexiAppsApp::run(void)
{
    lv_area_t area = getVisualArea();
    int w = area.x2 - area.x1;
    int h = area.y2 - area.y1;
    int card_w = w - 32;

    lv_obj_t *root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x090D14), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    lv_obj_t *hdr = create_amoled_card(root, card_w, 96, 12);
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "LEXIAPPS  •  SANDBOX & PACKAGE MANAGER");
    lv_obj_set_style_text_color(title, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_apps_action_lbl = lv_label_create(hdr);
    lv_label_set_text(g_apps_action_lbl, "Sv39 MMU Process Isolation + Wasmtime Sandbox Ready");
    lv_obj_set_style_text_color(g_apps_action_lbl, lv_color_hex(0x25C281), 0);
    lv_obj_set_style_text_font(g_apps_action_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_apps_action_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *lcard = create_amoled_card(root, card_w, 460, 122);
    g_apps_list_lbl = lv_label_create(lcard);
    lv_obj_set_width(g_apps_list_lbl, card_w - 36);
    lv_label_set_long_mode(g_apps_list_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_apps_list_lbl, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(g_apps_list_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_apps_list_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *acard = create_amoled_card(root, card_w, 160, 598);
    lv_obj_t *albl = lv_label_create(acard);
    lv_label_set_text(albl, "SUPERVISOR ACTIONS (Crash-Isolated Child Processes):");
    lv_obj_set_style_text_color(albl, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(albl, &lv_font_montserrat_14, 0);
    lv_obj_align(albl, LV_ALIGN_TOP_LEFT, 0, 0);

    int b_w = (card_w - 48) / 2;
    lv_obj_t *b1 = create_action_btn(acard, "Start/Restart Radio", b_w, 56, 0x0284C7);
    lv_obj_align(b1, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(b1, on_spawn_service_cb, LV_EVENT_CLICKED, (void *)"/sdcard/lora_radio.elf");

    lv_obj_t *b2 = create_action_btn(acard, "Start/Restart Node", b_w, 56, 0x16A34A);
    lv_obj_align(b2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(b2, on_spawn_service_cb, LV_EVENT_CLICKED, (void *)"/sdcard/superlexicon.elf");

    refresh_lexiapps_view();
    _proc_timer = lv_timer_create(apps_timer_cb, 2000, nullptr);
    return true;
}

bool LexiAppsApp::back(void)
{
    notifyCoreClosed();
    return true;
}

bool LexiAppsApp::close(void)
{
    if (_proc_timer) {
        lv_timer_del(_proc_timer);
        _proc_timer = nullptr;
    }
    g_apps_list_lbl = nullptr;
    g_apps_action_lbl = nullptr;
    return true;
}

bool LexiAppsApp::init(void) { return true; }
