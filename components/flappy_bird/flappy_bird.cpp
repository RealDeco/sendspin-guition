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

// ---- colors (non-sprite elements use original palette) ----
#define C_CLOUD    lv_color_make(255, 255, 255)
#define C_WHITE    lv_color_white()
#define C_BLACK    lv_color_black()

// ---- drawing helpers ----

static void fb_rect(lv_obj_t *cv, int x, int y, int w, int h,
                    lv_color_t bg, int radius = 0,
                    int bw = 0, lv_color_t bc = lv_color_black()) {
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color    = bg;
    d.bg_opa      = LV_OPA_COVER;
    d.radius      = radius;
    d.border_width = bw;
    if (bw > 0) d.border_color = bc;
    lv_canvas_draw_rect(cv, x, y, w, h, &d);
}

static void fb_text(lv_obj_t *cv, int y, const lv_font_t *font,
                    lv_color_t color, const char *txt) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font  = font;
    d.color = color;
    d.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(cv, 0, y, FB_W, &d, txt);
}

// ---- timer callback ----

void FlappyBirdGame::timer_cb_(lv_timer_t *t) {
    static_cast<FlappyBirdGame *>(t->user_data)->game_tick();
}

// ---- public API ----

void FlappyBirdGame::start_game() {
    if (state_ != FBState::IDLE) stop_game();
    init_();
    if (!canvas_) return;   // allocation failed
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
        bird_frame_ = 1;    // wings-up frame on tap
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
    size_t sz = (size_t)FB_W * FB_H * sizeof(lv_color_t);

#ifdef USE_ESP32
    buf_ = (uint8_t *) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf_)
        buf_ = (uint8_t *) heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    buf_ = new uint8_t[sz];
#endif

    if (!buf_) {
        ESP_LOGE(TAG, "Cannot allocate %u bytes for game canvas", (unsigned) sz);
        return;
    }
    memset(buf_, 0, sz);

    canvas_ = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas_, buf_, FB_W, FB_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(canvas_);

    timer_ = lv_timer_create(timer_cb_, 33, this);  // ~30 fps
}

void FlappyBirdGame::cleanup_() {
    if (timer_)  { lv_timer_del(timer_);  timer_  = nullptr; }
    if (canvas_) { lv_obj_del(canvas_);   canvas_ = nullptr; }
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
    bird_vel_    = 0.0f;
    bird_frame_  = 0;
    frame_cnt_   = 0;
    score_       = 0;
    death_ticks_ = 0;

    int min_gap_y = 35;
    int max_gap_y = FB_H - FB_GROUND - gap_size_ - 35;
    if (max_gap_y < min_gap_y) max_gap_y = min_gap_y;

    for (int i = 0; i < FB_NPIPES; i++) {
        pipes_[i].x       = FB_W + 50 + i * (FB_W / FB_NPIPES + 20);
        pipes_[i].gap_y   = min_gap_y + rand() % (max_gap_y - min_gap_y + 1);
        pipes_[i].scored  = false;
    }

    clouds_[0] = {10.0f,  28, 58};
    clouds_[1] = {120.0f, 45, 44};
    clouds_[2] = {195.0f, 18, 52};
}

// ---- game logic ----

void FlappyBirdGame::tick_playing_() {
    // Physics
    bird_vel_ += gravity_;
    if (bird_vel_ > 12.0f) bird_vel_ = 12.0f;
    bird_y_ += bird_vel_;

    if (bird_y_ < FB_BIRD_R) {
        bird_y_   = FB_BIRD_R;
        bird_vel_ = 1.0f;
    }
    if (bird_y_ >= FB_H - FB_GROUND - FB_BIRD_R) {
        bird_y_  = FB_H - FB_GROUND - FB_BIRD_R;
        state_   = FBState::DEAD;
        death_ticks_ = 0;
        render_();
        draw_game_over_();
        lv_obj_invalidate(canvas_);
        return;
    }

    // Wing animation (frame 1 = flap-up, resets when falling)
    if (bird_frame_ == 1 && bird_vel_ >= 0) bird_frame_ = 0;
    if (bird_frame_ != 1)
        bird_frame_ = (frame_cnt_ / 8) % 2 == 0 ? 0 : 2;

    // Clouds (scroll at 25% pipe speed)
    for (auto &c : clouds_) {
        c.x -= pipe_speed_ * 0.25f;
        if (c.x + c.w < 0) {
            c.x = FB_W + 5;
            c.y = 12 + rand() % 52;
            c.w = 40 + rand() % 28;
        }
    }

    // Pipes
    float max_x = 0;
    for (auto &p : pipes_) max_x = std::max(max_x, p.x);

    int min_gy = 35;
    int max_gy = FB_H - FB_GROUND - gap_size_ - 35;
    if (max_gy < min_gy) max_gy = min_gy;

    for (auto &p : pipes_) {
        p.x -= pipe_speed_;

        if (!p.scored && p.x + FB_PIPE_W / 2.0f < 120.0f) {
            p.scored = true;
            score_++;
        }
        if (p.x + FB_PIPE_W < 0) {
            p.x      = max_x + FB_W / (float)FB_NPIPES + 20;
            p.gap_y  = min_gy + rand() % (max_gy - min_gy + 1);
            p.scored = false;
            max_x    = p.x;
        }
    }

    // Collision
    if (check_collision_()) {
        state_       = FBState::DEAD;
        death_ticks_ = 0;
    }

    render_();
}

void FlappyBirdGame::tick_dead_() {
    death_ticks_++;
    // Brief fall after death (40 frames)
    if (death_ticks_ < 40) {
        bird_vel_ += gravity_;
        if (bird_vel_ > 12.0f) bird_vel_ = 12.0f;
        bird_y_ += bird_vel_;
        if (bird_y_ > FB_H - FB_GROUND - FB_BIRD_R)
            bird_y_ = FB_H - FB_GROUND - FB_BIRD_R;
    }
    render_();
    draw_game_over_();
    lv_obj_invalidate(canvas_);
}

bool FlappyBirdGame::check_collision_() {
    int bx = 120, by = (int) bird_y_;
    for (auto &p : pipes_) {
        int px = (int) p.x;
        bool x_hit = (bx + FB_BIRD_R - 4 > px) && (bx - FB_BIRD_R + 4 < px + FB_PIPE_W);
        if (!x_hit) continue;
        bool y_hit = (by - FB_BIRD_R + 4 < p.gap_y) ||
                     (by + FB_BIRD_R - 4 > p.gap_y + gap_size_);
        if (y_hit) return true;
    }
    return false;
}

// ---- rendering ----

void FlappyBirdGame::render_() {
    draw_bg_();
    draw_pipes_();
    draw_ground_();
    draw_bird_();
    draw_score_();
    lv_obj_invalidate(canvas_);
}

void FlappyBirdGame::draw_bg_() {
    lv_canvas_fill_bg(canvas_, FB_COL_SKY, LV_OPA_COVER);
    for (auto &c : clouds_) {
        int cx = (int) c.x;
        fb_rect(canvas_, cx,          c.y + 8,  c.w,      13, C_CLOUD, 8);
        fb_rect(canvas_, cx + 10,     c.y,      c.w - 18, 18, C_CLOUD, 9);
        fb_rect(canvas_, cx + 2,      c.y + 3,  22,       14, C_CLOUD, 9);
        fb_rect(canvas_, cx + c.w-22, c.y + 2,  20,       15, C_CLOUD, 9);
    }
}

void FlappyBirdGame::draw_pipes_() {
    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);

    for (auto &p : pipes_) {
        int px = (int) p.x;
        int gt = p.gap_y;
        int gb = gt + gap_size_;

        // Top pipe: body from screen top down to cap, then sprite cap facing down
        if (gt - FB_CAP_H > 0)
            fb_rect(canvas_, px, 0, FB_PIPE_W, gt - FB_CAP_H, FB_COL_PIPE);
        lv_canvas_draw_img(canvas_, px, gt - FB_CAP_H, &spr_pipe_cap_down, &img_dsc);

        // Bottom pipe: sprite cap facing up, then body to ground
        lv_canvas_draw_img(canvas_, px, gb, &spr_pipe_cap_up, &img_dsc);
        int body_top = gb + FB_CAP_H;
        int body_h   = FB_H - FB_GROUND - body_top;
        if (body_h > 0)
            fb_rect(canvas_, px, body_top, FB_PIPE_W, body_h, FB_COL_PIPE);
    }
}

void FlappyBirdGame::draw_ground_() {
    fb_rect(canvas_, 0, FB_H - FB_GROUND, FB_W, FB_GROUND, FB_COL_GROUND);
    fb_rect(canvas_, 0, FB_H - FB_GROUND, FB_W, 7,         FB_COL_GRASS);
}

void FlappyBirdGame::draw_bird_() {
    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    // Center sprite on the bird's physics position
    int bx = 120 - BIRD_SPR_W / 2;
    int by = (int) bird_y_ - BIRD_SPR_H / 2;
    lv_canvas_draw_img(canvas_, bx, by, spr_bird[bird_frame_], &img_dsc);
}

void FlappyBirdGame::draw_score_() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", score_);
    // Drop shadow then white
    fb_text(canvas_, 22, &lv_font_montserrat_20, C_BLACK, buf);
    fb_text(canvas_, 20, &lv_font_montserrat_20, C_WHITE, buf);
}

void FlappyBirdGame::draw_game_over_() {
    // Semi-transparent dark panel
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color     = C_BLACK;
    d.bg_opa       = LV_OPA_60;
    d.radius       = 14;
    d.border_width = 0;
    lv_canvas_draw_rect(canvas_, 28, 78, 184, 94, &d);

    fb_text(canvas_, 88,  &lv_font_montserrat_20, lv_color_make(255, 80, 80), "GAME OVER");

    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %d", score_);
    fb_text(canvas_, 116, &lv_font_montserrat_14, C_WHITE, buf);
    fb_text(canvas_, 142, &lv_font_montserrat_14, lv_color_make(180, 180, 180), "Hold button to exit");
}

}  // namespace flappy_bird
}  // namespace esphome

#endif  // USE_LVGL
