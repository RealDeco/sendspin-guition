#include "esphome/core/defines.h"
#ifdef USE_LVGL

#include "flappy_bird.h"
#include "sprites.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#endif

namespace esphome {
namespace flappy_bird {

static const char *const TAG = "flappy_bird";

// 3×5 pixel digit bitmaps (3 bits/row, MSB = left column)
static const uint8_t DIGIT_FONT[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b001, 0b001, 0b001},  // 7
    {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
};

// ---- ESPHome loop — drives game tick independent of LVGL scheduler ----

void FlappyBirdGame::loop() {
    if (state_ == FBState::IDLE || !canvas_) return;
    uint32_t now = millis();
    if (now - last_tick_ms_ >= FB_TICK_MS) {
        last_tick_ms_ = now;
        game_tick();
    }
}

// ---- public API ----

void FlappyBirdGame::start_game() {
    if (state_ != FBState::IDLE) stop_game();
    init_();
    if (!canvas_) return;

    // Position bird and clouds; pipes stay off-screen until PLAYING
    bird_y_     = FB_H / 2.0f - 15;
    bird_vel_   = 0;
    bird_frame_ = 0;
    bob_phase_  = 0;
    frame_cnt_  = 0;
    score_      = 0;
    clouds_[0]  = {10.0f,  28, CLOUD_SPR_W};
    clouds_[1]  = {120.0f, 38, CLOUD_SPR_W};
    clouds_[2]  = {195.0f, 18, CLOUD_SPR_W};

    last_tick_ms_ = millis();
    state_ = FBState::READY;
    ESP_LOGI(TAG, "Game ready");
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
    if (state_ == FBState::READY) {
        // First tap: launch into gameplay with an initial flap
        reset_pipes_();
        death_ticks_ = 0;
        state_       = FBState::PLAYING;
        bird_vel_    = flap_str_;
        bird_frame_  = 1;
    } else if (state_ == FBState::PLAYING) {
        bird_vel_   = flap_str_;
        bird_frame_ = 1;
    } else if (state_ == FBState::DEAD && death_ticks_ > 30) {
        // Tap to restart — go back to READY screen
        score_      = 0;
        bob_phase_  = 0;
        bird_y_     = FB_H / 2.0f - 15;
        bird_vel_   = 0;
        frame_cnt_  = 0;
        clouds_[0]  = {10.0f,  28, CLOUD_SPR_W};
        clouds_[1]  = {120.0f, 38, CLOUD_SPR_W};
        clouds_[2]  = {195.0f, 18, CLOUD_SPR_W};
        state_      = FBState::READY;
    }
}

void FlappyBirdGame::game_tick() {
    if (!canvas_) return;
    frame_cnt_++;
    switch (state_) {
        case FBState::READY:   tick_ready_();   break;
        case FBState::PLAYING: tick_playing_(); break;
        case FBState::DEAD:    tick_dead_();    break;
        default: break;
    }
}

// ---- lifecycle ----

void FlappyBirdGame::init_() {
    size_t sz = (size_t)FB_W * FB_H * 2;
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
}

void FlappyBirdGame::cleanup_() {
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

void FlappyBirdGame::reset_pipes_() {
    int min_gy = 35, max_gy = FB_H - FB_GROUND - gap_size_ - 35;
    if (max_gy < min_gy) max_gy = min_gy;
    for (int i = 0; i < FB_NPIPES; i++) {
        pipes_[i] = {(float)(FB_W + 50 + i * (FB_W / FB_NPIPES + 20)),
                     min_gy + rand() % (max_gy - min_gy + 1), false};
    }
}

// ---- game logic ----

void FlappyBirdGame::tick_ready_() {
    // Clouds drift slowly
    for (auto &c : clouds_) {
        c.x -= pipe_speed_ * 0.15f;
        if (c.x + c.w < 0) { c.x = FB_W + 5; c.y = 12 + rand() % 40; c.w = CLOUD_SPR_W; }
    }
    // Bird bobs up/down gently
    bob_phase_ += 0.2f;
    bird_y_     = (FB_H / 2.0f) - 20 + 8.0f * sinf(bob_phase_);
    bird_frame_ = (frame_cnt_ / 8) % 3;
    render_();
}

void FlappyBirdGame::tick_playing_() {
    bird_vel_ += gravity_;
    if (bird_vel_ > 20.0f) bird_vel_ = 20.0f;
    bird_y_ += bird_vel_;

    if (bird_y_ < FB_BIRD_R) { bird_y_ = FB_BIRD_R; bird_vel_ = 1.0f; }
    if (bird_y_ >= FB_H - FB_GROUND - FB_BIRD_R) {
        bird_y_ = FB_H - FB_GROUND - FB_BIRD_R;
        state_ = FBState::DEAD; death_ticks_ = 0;
        if (score_ > high_score_) high_score_ = score_;
        if (game_over_trigger_) game_over_trigger_->trigger(score_);
        render_(); return;
    }

    if (bird_frame_ == 1 && bird_vel_ >= 0) bird_frame_ = 0;
    if (bird_frame_ != 1) bird_frame_ = (frame_cnt_ / 8) % 2 == 0 ? 0 : 2;

    for (auto &c : clouds_) {
        c.x -= pipe_speed_ * 0.25f;
        if (c.x + c.w < 0) { c.x = FB_W + 5; c.y = 12 + rand() % 40; c.w = CLOUD_SPR_W; }
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

    if (check_collision_()) {
        state_ = FBState::DEAD; death_ticks_ = 0;
        if (score_ > high_score_) high_score_ = score_;
        if (game_over_trigger_) game_over_trigger_->trigger(score_);
    }
    render_();
}

void FlappyBirdGame::tick_dead_() {
    death_ticks_++;
    if (death_ticks_ < 40) {
        bird_vel_ += gravity_;
        if (bird_vel_ > 20.0f) bird_vel_ = 20.0f;
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

// ---- direct-buffer primitives ----

void FlappyBirdGame::fb_fill(int x, int y, int w, int h, uint16_t c) {
    int x0 = std::max(0, x),  x1 = std::min(FB_W, x + w);
    int y0 = std::max(0, y),  y1 = std::min(FB_H, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    for (int row = y0; row < y1; row++)
        std::fill(fb_ + row * FB_W + x0, fb_ + row * FB_W + x1, c);
}

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
                *dst = (uint16_t)(((src[2] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[0] >> 3));
            } else {
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

void FlappyBirdGame::fb_blend_rect(int x, int y, int w, int h, uint8_t opa) {
    int x0 = std::max(0, x),  x1 = std::min(FB_W, x + w);
    int y0 = std::max(0, y),  y1 = std::min(FB_H, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    uint8_t keep = 255 - opa;
    for (int row = y0; row < y1; row++) {
        uint16_t *p = fb_ + row * FB_W + x0;
        for (int col = x0; col < x1; col++, p++) {
            uint8_t r = (uint8_t)((((*p >> 11) & 0x1F) << 3) * keep >> 8);
            uint8_t g = (uint8_t)((((*p >>  5) & 0x3F) << 2) * keep >> 8);
            uint8_t b = (uint8_t)(((*p         & 0x1F) << 3) * keep >> 8);
            *p = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

void FlappyBirdGame::fb_digit(int x, int y, int d, int scale, uint16_t c) {
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 5; row++) {
        uint8_t bits = DIGIT_FONT[d][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (0b100 >> col))
                fb_fill(x + col * scale, y + row * scale, scale, scale, c);
        }
    }
}

void FlappyBirdGame::fb_score(int score) {
    const int scale = 3;
    const int dw    = 3 * scale + scale;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", score);
    int len     = (int) strlen(buf);
    int total_w = len * dw - scale;
    int sx      = (FB_W - total_w) / 2;
    for (int i = 0; i < len; i++)
        fb_digit(sx + i * dw, 16, buf[i] - '0', scale, COL_WHITE);
}

// ---- shared scene drawing ----

void FlappyBirdGame::draw_scene_() {
    fb_fill(0, 0, FB_W, FB_H, COL_SKY);
    for (auto &c : clouds_)
        fb_blit_alpha((int)c.x, c.y, spr_cloud_px, CLOUD_SPR_W, CLOUD_SPR_H);
    fb_fill(0, FB_H - FB_GROUND, FB_W, FB_GROUND, COL_GROUND);
    fb_fill(0, FB_H - FB_GROUND, FB_W, 7,         COL_GRASS);
}

void FlappyBirdGame::draw_pipes_() {
    for (auto &p : pipes_) {
        int px = (int)p.x;
        int gt = p.gap_y;
        int gb = gt + gap_size_;
        if (gt - FB_CAP_H > 0) fb_fill(px, 0, FB_PIPE_W, gt - FB_CAP_H, COL_PIPE);
        fb_blit_alpha(px, gt - FB_CAP_H, spr_pipe_cap_dn_px, PIPE_CAP_W, PIPE_CAP_H);
        fb_blit_alpha(px, gb,            spr_pipe_cap_up_px, PIPE_CAP_W, PIPE_CAP_H);
        int bt = gb + FB_CAP_H, bh = FB_H - FB_GROUND - bt;
        if (bh > 0) fb_fill(px, bt, FB_PIPE_W, bh, COL_PIPE);
    }
}

void FlappyBirdGame::draw_bird_() {
    static const uint8_t *bird_px[3] = {spr_bird_mid_px, spr_bird_up_px, spr_bird_dn_px};
    fb_blit_alpha(120 - BIRD_SPR_W / 2, (int)bird_y_ - BIRD_SPR_H / 2,
                  bird_px[bird_frame_], BIRD_SPR_W, BIRD_SPR_H);
}

// ---- render ----

void FlappyBirdGame::render_() {
    draw_scene_();

    if (state_ == FBState::PLAYING || state_ == FBState::DEAD) {
        draw_pipes_();
    }

    draw_bird_();

    if (state_ == FBState::PLAYING) {
        fb_score(score_);
        lv_obj_invalidate(canvas_);
        return;
    }

    // READY and DEAD need LVGL layer for text overlays
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

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

    if (state_ == FBState::READY) {
        // Semi-transparent title panel
        fb_blend_rect(30, 68, 180, 56, 120);
        txt(72,  lv_color_make(255, 220, 50), "FLAPPY BALL");
        txt(94,  lv_color_make(200, 255, 200), "Tap to flap!");
        if (high_score_ > 0) {
            char hs[24];
            snprintf(hs, sizeof(hs), "Best: %d", high_score_);
            txt(148, lv_color_make(255, 220, 100), hs);
        }
    }

    if (state_ == FBState::DEAD) {
        fb_score(score_);
        fb_blend_rect(20, 76, 200, 110, 153);
        txt(82,  lv_color_make(255, 80,  80),  "GAME OVER");
        char sc[24];
        snprintf(sc, sizeof(sc), "Score: %d", score_);
        txt(104, lv_color_white(), sc);
        if (high_score_ > 0) {
            char hs[24];
            snprintf(hs, sizeof(hs), "Best: %d", high_score_);
            txt(124, lv_color_make(255, 220, 100), hs);
        }
        txt(146, lv_color_make(180, 255, 180), "Tap to play again");
        txt(162, lv_color_make(160, 160, 160), "Hold to exit");
    }

    lv_canvas_finish_layer(canvas_, &layer);
    lv_obj_invalidate(canvas_);
}

}  // namespace flappy_bird
}  // namespace esphome

#endif  // USE_LVGL
