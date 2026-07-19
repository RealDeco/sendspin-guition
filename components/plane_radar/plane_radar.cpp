#include "esphome/core/defines.h"
#ifdef USE_LVGL

#include "plane_radar.h"
#include "font5x7.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#endif

namespace esphome {
namespace plane_radar {

static const char *const TAG = "plane_radar";

// ---- ESPHome loop ----

void PlaneRadar::loop() {
    if (state_ == PRState::IDLE || !canvas_) return;
    uint32_t now = millis();

    if (now - last_fetch_ms_ >= (uint32_t)update_interval_ms_) {
        last_fetch_ms_ = now;
        fetch_();
    }
    if (now - last_render_ms_ >= 200) {
        last_render_ms_ = now;
        render_();
    }
}

// ---- public API ----

void PlaneRadar::start() {
    if (state_ != PRState::IDLE) return;
    init_();
    if (!canvas_) return;
    aircraft_count_ = 0;
    last_fetch_ms_  = 0;
    last_render_ms_ = 0;
    state_ = PRState::ACTIVE;
    ESP_LOGI(TAG, "Radar started, range %d km", range_km_);
}

void PlaneRadar::stop() {
    state_ = PRState::IDLE;
    cleanup_();
    ESP_LOGI(TAG, "Radar stopped");
}

void PlaneRadar::toggle() {
    if (is_active()) stop();
    else             start();
}

void PlaneRadar::cycle_range() {
    int idx = N_RANGE_PRESETS - 1;
    for (int i = 0; i < N_RANGE_PRESETS; i++) {
        if (RANGE_PRESETS[i] >= range_km_) { idx = i; break; }
    }
    idx      = (idx + 1) % N_RANGE_PRESETS;
    range_km_ = RANGE_PRESETS[idx];
    ESP_LOGI(TAG, "Range -> %d km", range_km_);
}

// ---- lifecycle ----

void PlaneRadar::init_() {
    size_t sz = (size_t)PR_W * PR_H * sizeof(uint16_t);
#ifdef USE_ESP32
    fb_ = (uint16_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb_)
        fb_ = (uint16_t *)heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    fb_ = new uint16_t[PR_W * PR_H];
#endif
    if (!fb_) { ESP_LOGE(TAG, "Cannot allocate radar buffer"); return; }
    std::fill(fb_, fb_ + PR_W * PR_H, PR_COL_BG);

    canvas_ = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas_, fb_, PR_W, PR_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(canvas_);
}

void PlaneRadar::cleanup_() {
    if (canvas_) { lv_obj_delete(canvas_); canvas_ = nullptr; }
    if (fb_) {
#ifdef USE_ESP32
        heap_caps_free(fb_);
#else
        delete[] fb_;
#endif
        fb_ = nullptr;
    }
}

// ---- HTTP fetch ----

#ifdef USE_ESP32
static void *psram_malloc_(size_t sz) {
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

struct FetchCtx { char *buf; size_t len; size_t cap; };

static esp_err_t http_event_cb(esp_http_client_event_t *evt) {
    auto *ctx = (FetchCtx *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t copy = (size_t)evt->data_len;
        if (ctx->len + copy > ctx->cap) copy = ctx->cap - ctx->len;
        if (copy) {
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}
#endif

void PlaneRadar::fetch_() {
#ifdef USE_ESP32
    // Route cJSON allocations through PSRAM (set once)
    static bool hooks_set = false;
    if (!hooks_set) {
        cJSON_Hooks hooks = { psram_malloc_, heap_caps_free };
        cJSON_InitHooks(&hooks);
        hooks_set = true;
    }

    float dist_nm = (float)range_km_ * 0.539957f;
    char url[256];
    snprintf(url, sizeof(url),
        "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%.0f",
        center_lat_, center_lon_, dist_nm);

    constexpr size_t BUF_CAP = 131072;  // 128 KB in PSRAM
    char *resp = (char *)heap_caps_malloc(BUF_CAP + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resp) { ESP_LOGW(TAG, "Fetch buffer alloc failed"); return; }

    FetchCtx ctx = { resp, 0, BUF_CAP };

    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.event_handler     = http_event_cb;
    cfg.user_data         = &ctx;
    cfg.timeout_ms        = 6000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err    = esp_http_client_perform(client);
    int       status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Fetch failed: err=%d status=%d", err, status);
        heap_caps_free(resp);
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    heap_caps_free(resp);
    if (!root) { ESP_LOGW(TAG, "JSON parse failed"); return; }

    aircraft_count_ = 0;
    cJSON *ac_arr = cJSON_GetObjectItem(root, "ac");
    if (cJSON_IsArray(ac_arr)) {
        cJSON *item;
        cJSON_ArrayForEach(item, ac_arr) {
            if (aircraft_count_ >= MAX_AC) break;
            cJSON *jlat = cJSON_GetObjectItem(item, "lat");
            cJSON *jlon = cJSON_GetObjectItem(item, "lon");
            if (!cJSON_IsNumber(jlat) || !cJSON_IsNumber(jlon)) continue;

            Aircraft &a    = aircraft_[aircraft_count_];
            a.lat          = (float)jlat->valuedouble;
            a.lon          = (float)jlon->valuedouble;
            a.valid        = true;

            cJSON *jtrk    = cJSON_GetObjectItem(item, "track");
            a.track_deg    = cJSON_IsNumber(jtrk)  ? (float)jtrk->valuedouble  : 0.0f;
            cJSON *jgs     = cJSON_GetObjectItem(item, "gs");
            a.gs_knots     = cJSON_IsNumber(jgs)   ? (float)jgs->valuedouble   : 0.0f;
            cJSON *jalt    = cJSON_GetObjectItem(item, "alt_baro");
            a.alt_baro     = cJSON_IsNumber(jalt)  ? (int)jalt->valuedouble    : 0;

            cJSON *jflight = cJSON_GetObjectItem(item, "flight");
            if (cJSON_IsString(jflight) && jflight->valuestring) {
                strncpy(a.callsign, jflight->valuestring, 8);
                a.callsign[8] = '\0';
                for (int i = 7; i >= 0 && a.callsign[i] == ' '; i--)
                    a.callsign[i] = '\0';
            } else {
                a.callsign[0] = '\0';
            }
            aircraft_count_++;
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Fetched %d aircraft, range %d km", aircraft_count_, range_km_);

    if (aircraft_update_trigger_)
        aircraft_update_trigger_->trigger(aircraft_count_);
#endif
}

// ---- rendering ----

void PlaneRadar::geo_to_pixel_(float lat, float lon, int &px, int &py) {
    const float R = 6371.0f;
    float dlat = (lat - center_lat_) * (float)M_PI / 180.0f;
    float dlon = (lon - center_lon_) * (float)M_PI / 180.0f;
    float clat = center_lat_ * (float)M_PI / 180.0f;
    float dx_km = R * dlon * cosf(clat);
    float dy_km = R * dlat;
    px = PR_CX + (int)(dx_km / (float)range_km_ * PR_R);
    py = PR_CY - (int)(dy_km / (float)range_km_ * PR_R);
}

void PlaneRadar::render_() {
    if (!fb_ || !canvas_) return;

    // Background
    std::fill(fb_, fb_ + PR_W * PR_H, PR_COL_BG);

    // Range rings: 25%, 50%, 75%, 100% of PR_R
    draw_circle_(PR_CX, PR_CY, PR_R * 1 / 4, PR_COL_RING_DIM);
    draw_circle_(PR_CX, PR_CY, PR_R * 2 / 4, PR_COL_RING);
    draw_circle_(PR_CX, PR_CY, PR_R * 3 / 4, PR_COL_RING_DIM);
    draw_circle_(PR_CX, PR_CY, PR_R,          PR_COL_BORDER);

    // North indicator — small red triangle pointing up at top of outer ring
    fill_triangle_(PR_CX,     PR_CY - PR_R + 1,
                   PR_CX - 5, PR_CY - PR_R + 10,
                   PR_CX + 5, PR_CY - PR_R + 10, PR_COL_NORTH);

    // Center dot
    fill_circle_(PR_CX, PR_CY, 2, PR_COL_CENTER);

    // Aircraft
    for (int i = 0; i < aircraft_count_; i++) {
        const Aircraft &ac = aircraft_[i];
        if (!ac.valid) continue;
        int px, py;
        geo_to_pixel_(ac.lat, ac.lon, px, py);

        float dx   = (float)(px - PR_CX);
        float dy   = (float)(py - PR_CY);
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist > PR_R - 6) {
            float s = (PR_R - 6) / dist;
            fill_circle_((int)(PR_CX + dx * s), (int)(PR_CY + dy * s), 2, PR_COL_EDGE_AC);
        } else {
            draw_aircraft_(px, py, ac.track_deg);
            // Callsign: flip left/right to keep it inside the circle
            if (ac.callsign[0]) {
                int len  = (int)strlen(ac.callsign);
                int tw   = len * 6 - 1;
                int tx   = (px >= PR_CX) ? px + 10 : px - 10 - tw;
                int ty   = py - 3;
                // clamp so text stays inside outer ring
                tx = std::max(4, std::min(PR_W - tw - 4, tx));
                ty = std::max(4, std::min(PR_H - 11, ty));
                draw_text_(tx, ty, ac.callsign, PR_COL_CENTER, 1);
            }
        }
    }

    // Range label — scale 2, centered at bottom inside the ring
    char range_buf[10];
    if (use_miles_)
        snprintf(range_buf, sizeof(range_buf), "%dMI", (int)(range_km_ * 0.621371f));
    else
        snprintf(range_buf, sizeof(range_buf), "%dKM", range_km_);
    int rlen = (int)strlen(range_buf);
    int rx   = PR_CX - (rlen * 12) / 2;
    draw_text_(rx, PR_CY + PR_R - 20, range_buf, PR_COL_RING, 2);

    // Aircraft count — top-right, small
    char cnt_buf[8];
    snprintf(cnt_buf, sizeof(cnt_buf), "%d", aircraft_count_);
    draw_text_(PR_CX + 30, 10, cnt_buf, PR_COL_RING_DIM, 1);

    lv_obj_invalidate(canvas_);
}

void PlaneRadar::draw_aircraft_(int cx, int cy, float heading_deg) {
    float rad = heading_deg * (float)M_PI / 180.0f;
    float s = sinf(rad), c = cosf(rad);
    // Apex 10px forward, base 5px back, 6px wide
    int ax  = cx + (int)(10 * s);
    int ay  = cy - (int)(10 * c);
    int blx = cx + (int)(-5 * s - 6 * c);
    int bly = cy + (int)( 5 * c - 6 * s);
    int brx = cx + (int)(-5 * s + 6 * c);
    int bry = cy + (int)( 5 * c + 6 * s);
    fill_triangle_(ax, ay, blx, bly, brx, bry, PR_COL_AIRCRAFT);
}

// ---- draw primitives ----

void PlaneRadar::fill_rect_(int x, int y, int w, int h, uint16_t c) {
    int x0 = std::max(0, x), x1 = std::min(PR_W, x + w);
    int y0 = std::max(0, y), y1 = std::min(PR_H, y + h);
    for (int row = y0; row < y1; row++)
        std::fill(fb_ + row * PR_W + x0, fb_ + row * PR_W + x1, c);
}

void PlaneRadar::draw_circle_(int cx, int cy, int r, uint16_t c) {
    int x = r, y = 0, err = 0;
    while (x >= y) {
        set_pixel_(cx + x, cy + y, c); set_pixel_(cx - x, cy + y, c);
        set_pixel_(cx + x, cy - y, c); set_pixel_(cx - x, cy - y, c);
        set_pixel_(cx + y, cy + x, c); set_pixel_(cx - y, cy + x, c);
        set_pixel_(cx + y, cy - x, c); set_pixel_(cx - y, cy - x, c);
        y++;
        if (err <= 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void PlaneRadar::fill_circle_(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        fill_rect_(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

void PlaneRadar::fill_triangle_(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
    if (y0 > y2) { std::swap(x0, x2); std::swap(y0, y2); }
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
    int total = y2 - y0;
    if (total == 0) return;
    for (int y = y0; y <= y2; y++) {
        bool lower = (y >= y1);
        int  seg   = lower ? y2 - y1 : y1 - y0;
        float a    = (float)(y - y0) / total;
        float b    = seg ? (lower ? (float)(y - y1) / seg : (float)(y - y0) / seg) : 1.0f;
        int ax = x0 + (int)((x2 - x0) * a);
        int bx = lower ? x1 + (int)((x2 - x1) * b) : x0 + (int)((x1 - x0) * b);
        if (ax > bx) std::swap(ax, bx);
        fill_rect_(ax, y, bx - ax + 1, 1, c);
    }
}

void PlaneRadar::draw_char_(int x, int y, char ch, uint16_t c, int scale) {
    uint8_t idx = (uint8_t)ch;
    if (idx < 0x20 || idx > 0x5F) return;
    const uint8_t *g = FONT5X7[idx - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                if (scale == 1) set_pixel_(x + col, y + row, c);
                else            fill_rect_(x + col * scale, y + row * scale, scale, scale, c);
            }
        }
    }
}

void PlaneRadar::draw_text_(int x, int y, const char *text, uint16_t c, int scale) {
    int advance = (5 + 1) * scale;
    for (int i = 0; text[i]; i++)
        draw_char_(x + i * advance, y, text[i], c, scale);
}

}  // namespace plane_radar
}  // namespace esphome

#endif  // USE_LVGL
