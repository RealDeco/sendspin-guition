#include "esphome/core/defines.h"
#ifdef USE_LVGL

#include "flappy_bird.h"
#include "sprites.h"
#include "esphome/core/log.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#endif

namespace esphome {
namespace flappy_bird {

static const char *const TAG = "flappy_bird";

static FlappyBirdGame *g_game = nullptr;

// ---- timer callback ----

void FlappyBirdGame::timer_cb_(lv_timer_t *) {
    if (g_game) g_game->game_tick();
}

// ---- public API ----

void FlappyBirdGame::start_game() {
    if (state_ != FBState::IDLE) stop_game();
    init_();
    if (!canvas_) return;
    reset_();
    state_ = FBState::PLAYING;
    ESP_LOGI(TAG, "Game started");
}

void FlappyBirdGame::stop_game() {
    state_ = FBState::IDLE;
    cleanup_();
    ESP_LOGI(TAG, "Game stopped");
}

void FlappyBirdGame::toggle_game() {
    if (is_active()) stop_game();
    else             start_game();
}

void FlappyBirdGame::flap() {
    if (state_ == FBState::PLAYING) {
        bird_vel_   = flap_str_;
        bird_frame_ = 1;
    }
}

void FlappyBirdGame::game_tick() {
    if (!canvas_) return;
    frame_cnt_++;
    if      (state_ == FBState::PLAYING) tick_playing_();
    else if (state_ == FBState::DEAD)    tick_dead_();
}

// ---- lifecycle ----

void FlappyBirdGame::init_() {
    size_t sz = (size_t)FB_W * FB_H * 2;  // RGB565: 2 bytes per pixel
#ifdef USE_ESP32
    fb_ = (uint16_t *) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb_)
        fb_ = (uint16_t *) heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    fb_ = new uint16_t[FB_W * FB_H];
#endif
    if (!fb_) { ESP_LOGE(TAG, "Cannot allocate canvas buffer"); return; }
    memset(fb_, 0, sz);

    canvas_ = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas_, fb_, FB_W, FB_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(canvas_);

    g_game = this;
    timer_ = lv_timer_create(timer_cb_, 50, nullptr);  // 20 fps
}

void FlappyBirdGame::cleanup_() {
    g_game = nullptr;
    if (timer_)  { lv_timer_delete(timer_);  timer_  = nullptr; }
    if (canvas_) { lv_obj_delete(canvas_);   canvas_ = nullptr; }
    if (fb_) {
#ifdef USE_ESP32
        heap_caps_free(fb_);
#else
        delete[] fb_;
#endif
        fb_ = nullptr;
    }
}

void FlappyBirdGame::reset_() {
    bird_y_      = FB_H / 2.0f - 10;
    bird_vel_    = 0;
    bird_frame_  = 0;
    frame_cnt_   = 0;
    score_       = 0;
    death_ticks_ = 0;

    int min_gy = 35, max_gy = FB_H - FB_GROUND - gap_size_ - 35;
    if (max_gy < min_gy) max_gy = min_gy;

    for (int i = 0; i < FB_NPIPES; i++) {
        pipes_[i] = {(float)(FB_W + 50 + i * (FB_W / FB_NPIPES + 20)),
                     min_gy + rand() % (max_gy - min_gy + 1), false};
    }
    clouds_[0] = {10.0f,  28, 58};
    clouds_[1] = {120.0f, 45, 44};
    clouds_[2] = {195.0f, 18, 52};
}

// ---- game logic ----

void FlappyBirdGame::tick_playing_() {
    bird_vel_ += gravity_;
    if (bird_vel_ > 12.0f) bird_vel_ = 12.0f;
    bird_y_ += bird_vel_;

    if (bird_y_ < FB_BIRD_R) { bird_y_ = FB_BIRD_R; bird_vel_ = 1.0f; }
    if (bird_y_ >= FB_H - FB_GROUND - FB_BIRD_R) {
        bird_y_ = FB_H - FB_GROUND - FB_BIRD_R;
        state_ = FBState::DEAD; death_ticks_ = 0;
        render_(); return;
    }

    if (bird_frame_ == 1 && bird_vel_ >= 0) bird_frame_ = 0;
    if (bird_frame_ != 1) bird_frame_ = (frame_cnt_ / 8) % 2 == 0 ? 0 : 2;

    for (auto &c : clouds_) {
        c.x -= pipe_speed_ * 0.25f;
        if (c.x + c.w < 0) { c.x = FB_W + 5; c.y = 12 + rand() % 52; c.w = 40 + rand() % 28; }
    }

    int min_gy = 35, max_gy = FB_H - FB_GROUND - gap_size_ - 35;
    if (max_gy < min_gy) max_gy = min_gy;
    float max_x = 0;
    for (auto &p : pipes_) max_x = std::max(max_x, p.x);

    for (auto &p : pipes_) {
        p.x -= pipe_speed_;
        if (!p.scored && p.x + FB_PIPE_W / 2.0f < 120.0f) { p.scored = true; score_++; }
        if (p.x + FB_PIPE_W < 0) {
            p.x      = max_x + FB_W / (float)FB_NPIPES + 20;
            p.gap_y  = min_gy + rand() % (max_gy - min_gy + 1);
            p.scored = false;
            max_x    = p.x;
        }
    }

    if (check_collision_()) { state_ = FBState::DEAD; death_ticks_ = 0; }
    render_();
}

void FlappyBirdGame::tick_dead_() {
    death_ticks_++;
    if (death_ticks_ < 40) {
        bird_vel_ += gravity_;
        if (bird_vel_ > 12.0f) bird_vel_ = 12.0f;
        bird_y_ += bird_vel_;
        if (bird_y_ > FB_H - FB_GROUND - FB_BIRD_R) bird_y_ = FB_H - FB_GROUND - FB_BIRD_R;
    }
    render_();
}

bool FlappyBirdGame::check_collision_() {
    int bx = 120, by = (int) bird_y_;
    for (auto &p : pipes_) {
        int px = (int) p.x;
        bool xh = (bx + FB_BIRD_R - 4 > px) && (bx - FB_BIRD_R + 4 < px + FB_PIPE_W);
        if (!xh) continue;
        if ((by - FB_BIRD_R + 4 < p.gap_y) || (by + FB_BIRD_R - 4 > p.gap_y + gap_size_))
            return true;
    }
    return false;
}

// ---- direct-buffer drawing primitives ----

// Clipped fill into fb_
void FlappyBirdGame::fb_fill(int x, int y, int w, int h, uint16_t c) {
    int x0 = std::max(0, x),  x1 = std::min(FB_W, x + w);
    int y0 = std::max(0, y),  y1 = std::min(FB_H, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    int rw = x1 - x0;
    for (int row = y0; row < y1; row++) {
        uint16_t *p = fb_ + row * FB_W + x0;
        std::fill(p, p + rw, c);
    }
}

// Blit BGRA sprite with alpha blending (handles transparent pixels)
void FlappyBirdGame::fb_blit_alpha(int x, int y, const uint8_t *px, int sw, int sh) {
    for (int sy = 0; sy < sh; sy++) {
        int dy = y + sy;
        if (dy < 0 || dy >= FB_H) continue;
        const uint8_t *src = px + sy * sw * 4;
        for (int sx = 0; sx < sw; sx++, src += 4) {
            int dx = x + sx;
            if (dx < 0 || dx >= FB_W) continue;
            uint8_t a = src[3];
            if (a == 0) continue;
            uint16_t *dst = fb_ + dy * FB_W + dx;
            if (a == 255) {
                // Fully opaque: straight convert BGRA→RGB565
                *dst = (uint16_t)(((src[2] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[0] >> 3));
            } else {
                // Alpha blend — extract RGB565 background
                uint16_t bg = *dst;
                uint8_t bg_r = (uint8_t)((bg >> 11) << 3);
                uint8_t bg_g = (uint8_t)(((bg >> 5) & 0x3F) << 2);
                uint8_t bg_b = (uint8_t)((bg & 0x1F) << 3);
                uint8_t na   = 255 - a;
                uint8_t out_r = (uint8_t)(((uint16_t)src[2] * a + (uint16_t)bg_r * na) >> 8);
                uint8_t out_g = (uint8_t)(((uint16_t)src[1] * a + (uint16_t)bg_g * na) >> 8);
                uint8_t out_b = (uint8_t)(((uint16_t)src[0] * a + (uint16_t)bg_b * na) >> 8);
                *dst = (uint16_t)(((out_r >> 3) << 11) | ((out_g >> 2) << 5) | (out_b >> 3));
            }
        }
    }
}

// Darken a rect region (game-over panel) — blend to black at given opacity
void FlappyBirdGame::fb_blend_rect(int x, int y, int w, int h, uint8_t opa) {
    int x0 = std::max(0, x),  x1 = std::min(FB_W, x + w);
    int y0 = std::max(0, y),  y1 = std::min(FB_H, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    uint8_t keep = 255 - opa;  // portion of original to keep
    for (int row = y0; row < y1; row++) {
        uint16_t *p = fb_ + row * FB_W + x0;
        for (int col = x0; col < x1; col++, p++) {
            uint16_t px2 = *p;
            uint8_t r = (uint8_t)(((px2 >> 11) << 3) * keep >> 8);
            uint8_t g = (uint8_t)((((px2 >> 5) & 0x3F) << 2) * keep >> 8);
            uint8_t b = (uint8_t)(((px2 & 0x1F) << 3) * keep >> 8);
            *p = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

// ---- render ----

void FlappyBirdGame::render_() {
    // -- Phase 1: direct pixel writes (no LVGL overhead) --

    // Sky background
    fb_fill(0, 0, FB_W, FB_H, COL_SKY);

    // Clouds
    for (auto &c : clouds_)
        fb_fill((int)c.x, c.y, c.w, 16, COL_WHITE);

    // Pipes
    for (auto &p : pipes_) {
        int px = (int)p.x;
        int gt = p.gap_y;
        int gb = gt + gap_size_;

        // Top pipe body
        if (gt - FB_CAP_H > 0)
            fb_fill(px, 0, FB_PIPE_W, gt - FB_CAP_H, COL_PIPE);
        // Top pipe cap (facing down, has alpha edges)
        fb_blit_alpha(px, gt - FB_CAP_H, spr_pipe_cap_dn_px, PIPE_CAP_W, PIPE_CAP_H);

        // Bottom pipe cap (facing up, has alpha edges)
        fb_blit_alpha(px, gb, spr_pipe_cap_up_px, PIPE_CAP_W, PIPE_CAP_H);
        // Bottom pipe body
        int bt = gb + FB_CAP_H, bh = FB_H - FB_GROUND - bt;
        if (bh > 0)
            fb_fill(px, bt, FB_PIPE_W, bh, COL_PIPE);
    }

    // Ground
    fb_fill(0, FB_H - FB_GROUND, FB_W, FB_GROUND, COL_GROUND);
    fb_fill(0, FB_H - FB_GROUND, FB_W, 7,         COL_GRASS);

    // Bird (alpha sprite)
    static const uint8_t *bird_px[3] = {spr_bird_mid_px, spr_bird_up_px, spr_bird_dn_px};
    fb_blit_alpha(120 - BIRD_SPR_W / 2, (int)bird_y_ - BIRD_SPR_H / 2,
                  bird_px[bird_frame_], BIRD_SPR_W, BIRD_SPR_H);

    // Game-over darkening panel
    if (state_ == FBState::DEAD)
        fb_blend_rect(28, 78, 184, 94, 153);  // ~60% darken

    // -- Phase 2: LVGL layer for text only --
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    // Score
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", score_);
    {
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font       = LV_FONT_DEFAULT;
        d.color      = lv_color_white();
        d.align      = LV_TEXT_ALIGN_CENTER;
        d.text       = buf;
        d.text_local = 1;
        lv_area_t a  = {0, 20, (lv_coord_t)(FB_W - 1), 44};
        lv_draw_label(&layer, &d, &a);
    }

    // Game-over text lines
    if (state_ == FBState::DEAD) {
        auto txt = [&](int y, lv_color_t c, const char *s) {
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font       = LV_FONT_DEFAULT;
            d.color      = c;
            d.align      = LV_TEXT_ALIGN_CENTER;
            d.text       = s;
            d.text_local = 1;
            lv_area_t a  = {0, (lv_coord_t)y, (lv_coord_t)(FB_W - 1), (lv_coord_t)(y + 24)};
            lv_draw_label(&layer, &d, &a);
        };
        txt(88,  lv_color_make(255, 80, 80), "GAME OVER");
        char sc[24];
        snprintf(sc, sizeof(sc), "Score: %d", score_);
        txt(116, lv_color_white(), sc);
        txt(142, lv_color_make(180, 180, 180), "Hold to exit");
    }

    lv_canvas_finish_layer(canvas_, &layer);
    lv_obj_invalidate(canvas_);
}

}  // namespace flappy_bird
}  // namespace esphome

#endif  // USE_LVGL
