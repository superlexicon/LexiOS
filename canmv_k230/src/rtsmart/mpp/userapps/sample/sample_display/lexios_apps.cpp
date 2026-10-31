#include "lexios_apps.hpp"
#include "keyboard_ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

extern "C" {
LV_IMG_DECLARE(img_app_battery);
LV_IMG_DECLARE(img_app_live_call);
LV_IMG_DECLARE(img_app_gnss);
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
    bool nrf_on = lexios_get_nrf9151_power();
    bool kb_on = tca8418_is_connected();
    snprintf(buf, sizeof(buf),
             "LR2021 Radio: %s\nnRF9151 Sat/GNSS: %s\nQWERTY Keyboard: %s\nKPU / DPU / VPU: OFF (Power-Gated)",
             lr_state,
             nrf_on ? "ON (UART2 115200 8N1)" : "OFF (0mW Power-Gated)",
             kb_on ? "CONNECTED (I2C4 0x34)" : "DETACHED (Hot-Plug Ready)");
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
    if (lv_group_get_default() != nullptr) {
        lv_group_add_obj(lv_group_get_default(), g_chat_input_ta);
        lv_group_focus_obj(g_chat_input_ta);
    }

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

/* ============================================================================
 * 4. LexiSatApp — nRF9151 Satellite NTN (3GPP Rel-17), GNSS/GPS & QWERTY Hub
 * ============================================================================ */
#define LEXISAT_IOC_SET_BAUDRATE _IOW('U', 0x40, int)
struct lexisat_uart_cfg {
    uint32_t baud_rate;
    uint32_t data_bits   : 4;
    uint32_t stop_bits   : 2;
    uint32_t parity      : 2;
    uint32_t fifo_lenth  : 2;
    uint32_t auto_flow   : 1;
    uint32_t reserved    : 21;
};

static int g_sat_uart_fd = -1;
static pthread_mutex_t g_sat_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_sat_worker_started = false;
static volatile int g_sat_pending_op = 0; // 1=query_id, 2=mode_ntn, 3=mode_gnss, 4=mode_ltem, 5=custom_at
static char g_sat_custom_cmd[256] = {0};

static char g_sat_mode_str[32] = "STANDBY (OFF)";
static char g_sat_imei[32] = "--";
static char g_sat_iccid[32] = "--";
static char g_sat_fw[32] = "--";
static char g_sat_cereg[32] = "Unregistered";
static char g_sat_lat[24] = "--";
static char g_sat_lon[24] = "--";
static char g_sat_alt[24] = "--";
static char g_sat_speed[24] = "--";
static char g_sat_utc[32] = "-- (Wait Fix)";
static char g_sat_rx_log[512] =
    "[LexiSat] Nordic nRF9151 SiP (3GPP Rel-17 NTN Satellite / GNSS / LTE-M)\n"
    "[LexiSat] Tap 'nRF9151 ON' to power GPIO2 & attach /dev/uart2.";

static int g_kb_backlight_idx = 1; // 0=OFF(0%), 1=Low(5%), 2=High(25%)

static lv_obj_t *g_sat_hdr_lbl = nullptr;
static lv_obj_t *g_sat_gnss_lbl = nullptr;
static lv_obj_t *g_sat_log_lbl = nullptr;
static lv_obj_t *g_sat_input_ta = nullptr;

static void sat_append_log(const char *line)
{
    if (!line || !line[0]) return;
    pthread_mutex_lock(&g_sat_lock);
    size_t cur_len = strlen(g_sat_rx_log);
    if (cur_len > 340) {
        const char *nl = strchr(g_sat_rx_log + 100, '\n');
        if (nl) {
            memmove(g_sat_rx_log, nl + 1, strlen(nl + 1) + 1);
        } else {
            g_sat_rx_log[0] = '\0';
        }
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s\n%s", g_sat_rx_log, line);
    snprintf(g_sat_rx_log, sizeof(g_sat_rx_log), "%s", tmp);
    pthread_mutex_unlock(&g_sat_lock);
}

static bool sat_ensure_uart_open(void)
{
    if (!lexios_get_nrf9151_power()) {
        if (g_sat_uart_fd > 0) {
            close(g_sat_uart_fd);
            g_sat_uart_fd = -1;
        }
        return false;
    }
    if (g_sat_uart_fd > 0) {
        return true;
    }
    int fd = open("/dev/uart2", O_RDWR | O_NONBLOCK);
    if (fd <= 0) {
        return false;
    }
    struct lexisat_uart_cfg cfg = {
        .baud_rate = 115200,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = 0,
        .fifo_lenth = 2,
        .auto_flow = 0,
        .reserved = 0
    };
    ioctl(fd, LEXISAT_IOC_SET_BAUDRATE, &cfg);
    g_sat_uart_fd = fd;
    return true;
}

static void sat_parse_line(const char *line)
{
    if (!line || !line[0]) return;
    if (strcmp(line, "OK") == 0) return;

    pthread_mutex_lock(&g_sat_lock);
    if (strncmp(line, "#XGPS:", 6) == 0 && strlen(line) > 12) {
        /* Format: #XGPS: <lat>,<lon>,<alt>,<acc>,<speed>,<heading>,"YYYY-MM-DD HH:MM:SS" */
        char copy[160];
        snprintf(copy, sizeof(copy), "%s", line + 6);
        char *save = nullptr;
        char *tok = strtok_r(copy, ",", &save);
        int idx = 0;
        while (tok != nullptr) {
            while (*tok == ' ') tok++;
            if (idx == 0) snprintf(g_sat_lat, sizeof(g_sat_lat), "%s", tok);
            else if (idx == 1) snprintf(g_sat_lon, sizeof(g_sat_lon), "%s", tok);
            else if (idx == 2) snprintf(g_sat_alt, sizeof(g_sat_alt), "%s m", tok);
            else if (idx == 4) snprintf(g_sat_speed, sizeof(g_sat_speed), "%s m/s", tok);
            else if (idx == 6) {
                /* Strip quotes and sync system RTC */
                if (tok[0] == '"') tok++;
                size_t l = strlen(tok);
                if (l > 0 && tok[l - 1] == '"') tok[l - 1] = '\0';
                snprintf(g_sat_utc, sizeof(g_sat_utc), "%s UTC", tok);
                int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
                if (sscanf(tok, "%d-%d-%d %d:%d:%d", &y, &m, &d, &hh, &mm, &ss) == 6 && y >= 2024) {
                    struct tm tmv;
                    memset(&tmv, 0, sizeof(tmv));
                    tmv.tm_year = y - 1900;
                    tmv.tm_mon = m - 1;
                    tmv.tm_mday = d;
                    tmv.tm_hour = hh;
                    tmv.tm_min = mm;
                    tmv.tm_sec = ss;
                    time_t epoch = mktime(&tmv);
                    if (epoch > 0) {
                        struct timespec ts = {.tv_sec = epoch, .tv_nsec = 0};
                        clock_settime(CLOCK_REALTIME, &ts);
                    }
                }
            }
            tok = strtok_r(nullptr, ",", &save);
            idx++;
        }
    } else if (strstr(line, "%XICCID:") != nullptr) {
        const char *p = strchr(line, ':');
        if (p) {
            p++;
            while (*p == ' ') p++;
            snprintf(g_sat_iccid, sizeof(g_sat_iccid), "%s", p);
        }
    } else if (strstr(line, "mfw_") != nullptr) {
        snprintf(g_sat_fw, sizeof(g_sat_fw), "%s", line);
    } else if (strncmp(line, "+CEREG:", 7) == 0) {
        snprintf(g_sat_cereg, sizeof(g_sat_cereg), "%s", line + 7);
    } else if (strlen(line) == 15 && line[0] >= '0' && line[0] <= '9') {
        snprintf(g_sat_imei, sizeof(g_sat_imei), "%s", line);
    }
    pthread_mutex_unlock(&g_sat_lock);

    char log_entry[160];
    snprintf(log_entry, sizeof(log_entry), "RX < %s", line);
    sat_append_log(log_entry);
}

static void sat_send_at(const char *cmd, int wait_ms)
{
    if (!sat_ensure_uart_open()) return;
    char full[280];
    snprintf(full, sizeof(full), "%s\r\n", cmd);
    write(g_sat_uart_fd, full, strlen(full));

    char log_entry[160];
    snprintf(log_entry, sizeof(log_entry), "TX > %s", cmd);
    sat_append_log(log_entry);

    int loops = (wait_ms > 0 ? wait_ms : 200) / 40;
    if (loops < 1) loops = 1;
    char rx_buf[512];
    for (int i = 0; i < loops; i++) {
        usleep(40000);
        int n = read(g_sat_uart_fd, rx_buf, sizeof(rx_buf) - 1);
        if (n > 0) {
            rx_buf[n] = '\0';
            char *save = nullptr;
            char *line = strtok_r(rx_buf, "\r\n", &save);
            while (line != nullptr) {
                sat_parse_line(line);
                line = strtok_r(nullptr, "\r\n", &save);
            }
        }
    }
}

static void *sat_uart_worker_thread(void *arg)
{
    (void)arg;
    char rx_buf[512];
    while (1) {
        int op = g_sat_pending_op;
        if (op != 0) {
            g_sat_pending_op = 0;
            if (!lexios_get_nrf9151_power()) {
                lexios_set_nrf9151_power(true);
                pthread_mutex_lock(&g_sat_lock);
                snprintf(g_sat_mode_str, sizeof(g_sat_mode_str), "POWERED ON");
                pthread_mutex_unlock(&g_sat_lock);
            }
            if (op == 1) {
                /* Query Modem ID & Registration */
                sat_send_at("AT+CGMR", 250);
                sat_send_at("AT+CGSN", 250);
                sat_send_at("AT%XICCID", 250);
                sat_send_at("AT+CEREG?", 250);
            } else if (op == 2) {
                /* 3GPP Rel-17 Satellite NTN NB-IoT Mode */
                pthread_mutex_lock(&g_sat_lock);
                snprintf(g_sat_mode_str, sizeof(g_sat_mode_str), "SAT-NTN (3GPP Rel-17)");
                pthread_mutex_unlock(&g_sat_lock);
                sat_send_at("AT#XGPS=0", 250);
                sat_send_at("AT+CFUN=0", 400);
                sat_send_at("AT%XSYSTEMMODE=0,0,0,0,1", 400);
                sat_send_at("AT+CEREG=5", 250);
                sat_send_at("AT+CFUN=1", 500);
                sat_send_at("AT+CEREG?", 300);
            } else if (op == 3) {
                /* GNSS / GPS Positioning Mode */
                pthread_mutex_lock(&g_sat_lock);
                snprintf(g_sat_mode_str, sizeof(g_sat_mode_str), "GNSS (GPS Active)");
                pthread_mutex_unlock(&g_sat_lock);
                sat_send_at("AT#XGPS=0", 300);
                sat_send_at("AT+CFUN=0", 400);
                sat_send_at("AT%XSYSTEMMODE=0,0,1,0", 400);
                sat_send_at("AT+CFUN=31", 400);
                sat_send_at("AT#XGPS=1,0,0,0", 400);
            } else if (op == 4) {
                /* LTE-M + GNSS Concurrent Mode */
                pthread_mutex_lock(&g_sat_lock);
                snprintf(g_sat_mode_str, sizeof(g_sat_mode_str), "LTE-M + GNSS");
                pthread_mutex_unlock(&g_sat_lock);
                sat_send_at("AT+CFUN=0", 400);
                sat_send_at("AT%XSYSTEMMODE=1,0,1,0", 400);
                sat_send_at("AT+CEREG=5", 250);
                sat_send_at("AT+CFUN=1", 400);
                sat_send_at("AT#XGPS=1,0,0,0", 400);
            } else if (op == 5) {
                char cmd_copy[256];
                pthread_mutex_lock(&g_sat_lock);
                snprintf(cmd_copy, sizeof(cmd_copy), "%s", g_sat_custom_cmd);
                pthread_mutex_unlock(&g_sat_lock);
                if (cmd_copy[0]) {
                    sat_send_at(cmd_copy, 350);
                }
            }
        }

        /* Background non-blocking poll for unsolicited #XGPS: / +CEREG: sentences */
        if (sat_ensure_uart_open()) {
            int n = read(g_sat_uart_fd, rx_buf, sizeof(rx_buf) - 1);
            if (n > 0) {
                rx_buf[n] = '\0';
                char *save = nullptr;
                char *line = strtok_r(rx_buf, "\r\n", &save);
                while (line != nullptr) {
                    sat_parse_line(line);
                    line = strtok_r(nullptr, "\r\n", &save);
                }
            }
        }
        usleep(100000);
    }
    return nullptr;
}

static void refresh_lexisat_ui(void)
{
    if (!g_sat_hdr_lbl) return;
    bool nrf_on = lexios_get_nrf9151_power();
    bool kb_on = tca8418_is_connected();
    const char *kb_light = (g_kb_backlight_idx == 0) ? "OFF" :
                           (g_kb_backlight_idx == 1) ? "Low (5%)" : "High (25%)";

    char buf[512];
    pthread_mutex_lock(&g_sat_lock);
    snprintf(buf, sizeof(buf),
             "nRF9151: %s  |  Mode: %s\n"
             "QWERTY KB (I2C4 0x34): %s  |  Backlight: %s",
             nrf_on ? "ON (3.3V GPIO2)" : "OFF (0mW Standby)",
             nrf_on ? g_sat_mode_str : "STANDBY",
             kb_on ? "CONNECTED" : "DETACHED",
             kb_light);
    lv_label_set_text(g_sat_hdr_lbl, buf);

    if (g_sat_gnss_lbl) {
        snprintf(buf, sizeof(buf),
                 "GPS Fix   : Lat %s  |  Lon %s\n"
                 "Kinematics: Alt %s  |  Speed %s  |  %s\n"
                 "Modem ID  : IMEI %s  |  FW %s  |  CEREG %s",
                 g_sat_lat, g_sat_lon,
                 g_sat_alt, g_sat_speed, g_sat_utc,
                 g_sat_imei, g_sat_fw, g_sat_cereg);
        lv_label_set_text(g_sat_gnss_lbl, buf);
    }
    if (g_sat_log_lbl) {
        lv_label_set_text(g_sat_log_lbl, g_sat_rx_log);
    }
    pthread_mutex_unlock(&g_sat_lock);
}

static void sat_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_lexisat_ui();
}

static void on_sat_power_toggle_cb(lv_event_t *e)
{
    (void)e;
    bool cur = lexios_get_nrf9151_power();
    if (cur) {
        if (g_sat_uart_fd > 0) {
            write(g_sat_uart_fd, "AT+CFUN=0\r\n", 11);
            usleep(80000);
            close(g_sat_uart_fd);
            g_sat_uart_fd = -1;
        }
        lexios_set_nrf9151_power(false);
        sat_append_log("[LexiSat] Powered OFF nRF9151 (GPIO2 LOW, GPIO5 locked HIGH).");
    } else {
        lexios_set_nrf9151_power(true);
        sat_append_log("[LexiSat] Powered ON nRF9151 (GPIO2 HIGH, UART2 115200 active).");
        g_sat_pending_op = 1; // Query Modem ID automatically on power-up
    }
    refresh_lexisat_ui();
}

static void on_sat_op_btn_cb(lv_event_t *e)
{
    intptr_t op = (intptr_t)lv_event_get_user_data(e);
    g_sat_pending_op = (int)op;
    refresh_lexisat_ui();
}

static void on_sat_kb_rescan_cb(lv_event_t *e)
{
    (void)e;
    tca8418_init();
    if (tca8418_is_connected()) {
        int duty = (g_kb_backlight_idx == 0) ? 0 : (g_kb_backlight_idx == 1) ? 5 : 25;
        control_keyboard_backlight(20000, duty);
        sat_append_log("[LexiSat] Physical QWERTY Keyboard detected on I2C4 (0x34)!");
    } else {
        sat_append_log("[LexiSat] Physical Keyboard not detected on I2C4 (0x34).");
    }
    refresh_lexisat_ui();
}

static void on_sat_kb_light_cb(lv_event_t *e)
{
    (void)e;
    g_kb_backlight_idx = (g_kb_backlight_idx + 1) % 3;
    int duty = (g_kb_backlight_idx == 0) ? 0 : (g_kb_backlight_idx == 1) ? 5 : 25;
    control_keyboard_backlight(20000, duty);
    refresh_lexisat_ui();
}

static void on_sat_send_sos_cb(lv_event_t *e)
{
    (void)e;
    char sos_payload[256];
    pthread_mutex_lock(&g_sat_lock);
    snprintf(sos_payload, sizeof(sos_payload),
             "SOS/POS [LexiOS K230] Lat:%s Lon:%s Alt:%s UTC:%s",
             g_sat_lat, g_sat_lon, g_sat_alt, g_sat_utc);
    snprintf(g_sat_custom_cmd, sizeof(g_sat_custom_cmd), "AT#XSEND=\"%s\"", sos_payload);
    pthread_mutex_unlock(&g_sat_lock);

    /* 1. Queue transmission over nRF9151 Satellite NTN / Cellular UART2 */
    g_sat_pending_op = 5;

    /* 2. Simultaneously broadcast emergency coordinates over SuperLexicon FLRC Mesh */
    bool mesh_ok = (submit_mesh_chat_rpc("#emergency", sos_payload) == 0);
    char log_line[300];
    snprintf(log_line, sizeof(log_line), "SOS TX > Sat NTN + FLRC Mesh (%s): %s",
             mesh_ok ? "RPC OK" : "Queued", sos_payload);
    sat_append_log(log_line);
    refresh_lexisat_ui();
}

static void on_sat_send_at_cb(lv_event_t *e)
{
    (void)e;
    if (!g_sat_input_ta) return;
    const char *txt = lv_textarea_get_text(g_sat_input_ta);
    if (!txt || !txt[0]) return;
    pthread_mutex_lock(&g_sat_lock);
    snprintf(g_sat_custom_cmd, sizeof(g_sat_custom_cmd), "%s", txt);
    pthread_mutex_unlock(&g_sat_lock);
    g_sat_pending_op = 5;
    lv_textarea_set_text(g_sat_input_ta, "");
    refresh_lexisat_ui();
}

LexiSatApp::LexiSatApp(bool use_status_bar, bool use_navigation_bar)
    : ESP_Brookesia_PhoneApp(
          ESP_BROOKESIA_CORE_APP_DATA_DEFAULT("LexiSat", &img_app_gnss, true),
          ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(&img_app_gnss, use_status_bar, use_navigation_bar))
{}

LexiSatApp::~LexiSatApp() {}

bool LexiSatApp::run(void)
{
    if (!g_sat_worker_started) {
        g_sat_worker_started = true;
        pthread_t tid;
        if (pthread_create(&tid, nullptr, sat_uart_worker_thread, nullptr) == 0) {
            pthread_detach(tid);
        }
    }

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

    /* 1. Header Card */
    lv_obj_t *hdr = create_amoled_card(root, card_w, 96, 8);
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "LEXISAT  •  NTN SATELLITE, GNSS & KB");
    lv_obj_set_style_text_color(title, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_sat_hdr_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_color(g_sat_hdr_lbl, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(g_sat_hdr_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sat_hdr_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* 2. Power, 3GPP Rel-17 Satellite / GNSS Mode & Keyboard Controls Card */
    lv_obj_t *mcard = create_amoled_card(root, card_w, 136, 112);
    int b4_w = (card_w - 56) / 4;

    lv_obj_t *b_pwr = create_action_btn(mcard, "nRF9151 ON/OFF", b4_w, 46, 0x0F766E);
    lv_obj_align(b_pwr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(b_pwr, on_sat_power_toggle_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *b_ntn = create_action_btn(mcard, "SAT NTN", b4_w, 46, 0x7C3AED);
    lv_obj_align(b_ntn, LV_ALIGN_TOP_LEFT, b4_w + 8, 0);
    lv_obj_add_event_cb(b_ntn, on_sat_op_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)2);

    lv_obj_t *b_gps = create_action_btn(mcard, "GNSS Fix", b4_w, 46, 0x0284C7);
    lv_obj_align(b_gps, LV_ALIGN_TOP_LEFT, (b4_w + 8) * 2, 0);
    lv_obj_add_event_cb(b_gps, on_sat_op_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)3);

    lv_obj_t *b_lte = create_action_btn(mcard, "LTE-M+GPS", b4_w, 46, 0x2563EB);
    lv_obj_align(b_lte, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(b_lte, on_sat_op_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)4);

    /* Second row: Modem ID, SOS Beacon, Keyboard Re-Scan & Backlight */
    lv_obj_t *b_id = create_action_btn(mcard, "Query ID", b4_w, 46, 0x334155);
    lv_obj_align(b_id, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(b_id, on_sat_op_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    lv_obj_t *b_sos = create_action_btn(mcard, "TX SOS+GPS", b4_w, 46, 0xDC2626);
    lv_obj_align(b_sos, LV_ALIGN_BOTTOM_LEFT, b4_w + 8, 0);
    lv_obj_add_event_cb(b_sos, on_sat_send_sos_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *b_kbs = create_action_btn(mcard, "Re-Scan KB", b4_w, 46, 0x4F46E5);
    lv_obj_align(b_kbs, LV_ALIGN_BOTTOM_LEFT, (b4_w + 8) * 2, 0);
    lv_obj_add_event_cb(b_kbs, on_sat_kb_rescan_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *b_kbl = create_action_btn(mcard, "KB Light", b4_w, 46, 0xD97706);
    lv_obj_align(b_kbl, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(b_kbl, on_sat_kb_light_cb, LV_EVENT_CLICKED, nullptr);

    /* 3. Live GNSS Telemetry & Modem Identity Card */
    lv_obj_t *gcard = create_amoled_card(root, card_w, 106, 256);
    g_sat_gnss_lbl = lv_label_create(gcard);
    lv_obj_set_width(g_sat_gnss_lbl, card_w - 32);
    lv_label_set_long_mode(g_sat_gnss_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_sat_gnss_lbl, lv_color_hex(0x25C281), 0);
    lv_obj_set_style_text_font(g_sat_gnss_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sat_gnss_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 4. Live UART2 / Satellite Transceiver Log Card */
    lv_obj_t *lcard = create_amoled_card(root, card_w, 134, 370);
    g_sat_log_lbl = lv_label_create(lcard);
    lv_obj_set_width(g_sat_log_lbl, card_w - 32);
    lv_label_set_long_mode(g_sat_log_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_sat_log_lbl, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(g_sat_log_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(g_sat_log_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 5. AT Command / Satellite Payload Input + Touch & Physical Keyboard */
    g_sat_input_ta = lv_textarea_create(root);
    lv_obj_set_size(g_sat_input_ta, card_w - 124, 54);
    lv_obj_align(g_sat_input_ta, LV_ALIGN_TOP_LEFT, 16, 512);
    lv_textarea_set_one_line(g_sat_input_ta, true);
    lv_textarea_set_placeholder_text(g_sat_input_ta, "AT command or Sat NTN payload...");
    if (lv_group_get_default() != nullptr) {
        lv_group_add_obj(lv_group_get_default(), g_sat_input_ta);
        lv_group_focus_obj(g_sat_input_ta);
    }

    lv_obj_t *send_btn = create_action_btn(root, "SEND AT", 112, 54, 0x16A34A);
    lv_obj_align(send_btn, LV_ALIGN_TOP_RIGHT, -16, 512);
    lv_obj_add_event_cb(send_btn, on_sat_send_at_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *kb = lv_keyboard_create(root);
    lv_obj_set_size(kb, w, 310);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, g_sat_input_ta);

    refresh_lexisat_ui();
    _sat_timer = lv_timer_create(sat_timer_cb, 1000, nullptr);
    return true;
}

bool LexiSatApp::back(void)
{
    notifyCoreClosed();
    return true;
}

bool LexiSatApp::close(void)
{
    if (_sat_timer) {
        lv_timer_del(_sat_timer);
        _sat_timer = nullptr;
    }
    g_sat_hdr_lbl = nullptr;
    g_sat_gnss_lbl = nullptr;
    g_sat_log_lbl = nullptr;
    g_sat_input_ta = nullptr;
    return true;
}

bool LexiSatApp::init(void) { return true; }
