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

// Static instance pointer — avoids accessing opaque lv_timer_t internals
static FlappyBirdGame *g_game = nullptr;

// ---- drawing helpers (LVGL 9 layer-based API) ----

static void fb_rect(lv_layer_t *l, int x, int y, int w, int h,
                    lv_color_t c, int r = 0) {
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color    = c;
    d.bg_opa      = LV_OPA_COVER;
    d.radius      = r;
    d.border_width = 0;
    lv_area_t a = {(lv_coord_t)x, (lv_coord_t)y,
                   (lv_coord_t)(x + w - 1), (lv_coord_t)(y + h - 1)};
    lv_draw_rect(l, &d, &a);
}

static void fb_rect_opa(lv_layer_t *l, int x, int y, int w, int h,
                        lv_color_t c, lv_opa_t opa, int r = 0) {
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color    = c;
    d.bg_opa      = opa;
    d.radius      = r;
    d.border_width = 0;
    lv_area_t a = {(lv_coord_t)x, (lv_coord_t)y,
                   (lv_coord_t)(x + w - 1), (lv_coord_t)(y + h - 1)};
    lv_draw_rect(l, &d, &a);
}

static void fb_img(lv_layer_t *l, int x, int y, const lv_image_dsc_t *s) {
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = s;
    lv_area_t a = {(lv_coord_t)x, (lv_coord_t)y,
                   (lv_coord_t)(x + (int)s->header.w - 1),
                   (lv_coord_t)(y + (int)s->header.h - 1)};
    lv_draw_image(l, &d, &a);
}

static void fb_text(lv_layer_t *l, int y, lv_color_t c, const char *txt) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font       = LV_FONT_DEFAULT;
    d.color      = c;
    d.align      = LV_TEXT_ALIGN_CENTER;
    d.text       = txt;
    d.text_local = 1;
    lv_area_t a  = {0, (lv_coord_t)y, (lv_coord_t)(FB_W - 1), (lv_coord_t)(y + 24)};
    lv_draw_label(l, &d, &a);
}

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
    // Canvas buffer — RGB565, 2 bytes/pixel
    size_t sz = (size_t)FB_W * FB_H * 2;
#ifdef USE_ESP32
    buf_ = (uint8_t *) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf_)
        buf_ = (uint8_t *) heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    buf_ = new uint8_t[sz];
#endif
    if (!buf_) { ESP_LOGE(TAG, "Cannot allocate canvas buffer"); return; }
    memset(buf_, 0, sz);

    canvas_ = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas_, buf_, FB_W, FB_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(canvas_);

    // Initialise sprite descriptors (runtime, avoids C++ designated-initialiser limits)
    auto mk = [](lv_image_dsc_t &d, uint16_t w, uint16_t h, const uint8_t *px) {
        memset(&d, 0, sizeof(d));
        d.header.magic  = LV_IMAGE_HEADER_MAGIC;
        d.header.cf     = LV_COLOR_FORMAT_ARGB8888;
        d.header.w      = w;
        d.header.h      = h;
        d.header.stride = (uint16_t)(w * 4);
        d.data_size     = (uint32_t)(w * h * 4);
        d.data          = px;
    };
    mk(bird_sprs_[0], BIRD_SPR_W, BIRD_SPR_H, spr_bird_mid_px);
    mk(bird_sprs_[1], BIRD_SPR_W, BIRD_SPR_H, spr_bird_up_px);
    mk(bird_sprs_[2], BIRD_SPR_W, BIRD_SPR_H, spr_bird_dn_px);
    mk(pipe_cap_up_,  PIPE_CAP_W, PIPE_CAP_H, spr_pipe_cap_up_px);
    mk(pipe_cap_dn_,  PIPE_CAP_W, PIPE_CAP_H, spr_pipe_cap_dn_px);

    g_game = this;
    timer_ = lv_timer_create(timer_cb_, 33, nullptr);  // ~30 fps
}

void FlappyBirdGame::cleanup_() {
    g_game = nullptr;
    if (timer_)  { lv_timer_delete(timer_);  timer_  = nullptr; }
    if (canvas_) { lv_obj_delete(canvas_);   canvas_ = nullptr; }
    if (buf_) {
#ifdef USE_ESP32
        heap_caps_free(buf_);
#else
        delete[] buf_;
#endif
        buf_ = nullptr;
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

// ---- rendering ----

void FlappyBirdGame::render_() {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    draw_bg_(&layer);
    draw_pipes_(&layer);
    draw_ground_(&layer);
    draw_bird_(&layer);
    draw_score_(&layer);
    if (state_ == FBState::DEAD) draw_game_over_(&layer);

    lv_canvas_finish_layer(canvas_, &layer);
    lv_obj_invalidate(canvas_);
}

void FlappyBirdGame::draw_bg_(lv_layer_t *l) {
    // Sky fill
    fb_rect(l, 0, 0, FB_W, FB_H, FB_COL_SKY);
    // Clouds (3 overlapping rounded rects per cloud)
    lv_color_t white = lv_color_white();
    for (auto &c : clouds_) {
        int cx = (int) c.x;
        fb_rect(l, cx,          c.y + 8,  c.w,      13, white, 8);
        fb_rect(l, cx + 10,     c.y,      c.w - 18, 18, white, 9);
        fb_rect(l, cx + 2,      c.y + 3,  22,       14, white, 9);
        fb_rect(l, cx + c.w-22, c.y + 2,  20,       15, white, 9);
    }
}

void FlappyBirdGame::draw_pipes_(lv_layer_t *l) {
    for (auto &p : pipes_) {
        int px = (int) p.x;
        int gt = p.gap_y;
        int gb = gt + gap_size_;

        // Top pipe: body then cap facing down
        if (gt - FB_CAP_H > 0) fb_rect(l, px, 0, FB_PIPE_W, gt - FB_CAP_H, FB_COL_PIPE);
        fb_img(l, px, gt - FB_CAP_H, &pipe_cap_dn_);

        // Bottom pipe: cap facing up then body
        fb_img(l, px, gb, &pipe_cap_up_);
        int bt = gb + FB_CAP_H, bh = FB_H - FB_GROUND - bt;
        if (bh > 0) fb_rect(l, px, bt, FB_PIPE_W, bh, FB_COL_PIPE);
    }
}

void FlappyBirdGame::draw_ground_(lv_layer_t *l) {
    fb_rect(l, 0, FB_H - FB_GROUND, FB_W, FB_GROUND, FB_COL_GROUND);
    fb_rect(l, 0, FB_H - FB_GROUND, FB_W, 7,         FB_COL_GRASS);
}

void FlappyBirdGame::draw_bird_(lv_layer_t *l) {
    int bx = 120 - BIRD_SPR_W / 2;
    int by = (int) bird_y_ - BIRD_SPR_H / 2;
    fb_img(l, bx, by, &bird_sprs_[bird_frame_]);
}

void FlappyBirdGame::draw_score_(lv_layer_t *l) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", score_);
    fb_text(l, 22, lv_color_black(), buf);
    fb_text(l, 20, lv_color_white(), buf);
}

void FlappyBirdGame::draw_game_over_(lv_layer_t *l) {
    fb_rect_opa(l, 28, 78, 184, 94, lv_color_black(), LV_OPA_60, 14);
    fb_text(l, 88,  lv_color_make(255, 80, 80), "GAME OVER");
    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %d", score_);
    fb_text(l, 116, lv_color_white(), buf);
    fb_text(l, 142, lv_color_make(180, 180, 180), "Hold to exit");
}

}  // namespace flappy_bird
}  // namespace esphome

#endif  // USE_LVGL
