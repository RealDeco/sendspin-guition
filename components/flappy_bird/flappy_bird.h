#pragma once

#include "esphome/core/defines.h"
#ifdef USE_LVGL

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "lvgl.h"

namespace esphome {
namespace flappy_bird {

static const int FB_W      = 240;
static const int FB_H      = 240;
static const int FB_PIPE_W = 36;
static const int FB_BIRD_R = 12;
static const int FB_NPIPES = 2;
static const int FB_GROUND = 22;
static const int FB_CAP_H  = 16;

// Pre-computed RGB565 palette (little-endian, matches LVGL canvas format)
static constexpr uint16_t c565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static constexpr uint16_t COL_SKY    = c565(78,  192, 202);
static constexpr uint16_t COL_GROUND = c565(222, 216, 149);
static constexpr uint16_t COL_GRASS  = c565(112, 185, 68);
static constexpr uint16_t COL_PIPE   = c565(115, 177, 56);
static constexpr uint16_t COL_WHITE  = 0xFFFF;
static constexpr uint16_t COL_BLACK  = 0x0000;

enum class FBState { IDLE, PLAYING, DEAD };
struct FBPipe  { float x; int gap_y; bool scored; };
struct FBCloud { float x; int y; int w; };

class FlappyBirdGame : public Component {
 public:
    void setup() override {}
    void loop()  override {}
    float get_setup_priority() const override { return setup_priority::LATE; }

    void set_gravity(float v)       { gravity_    = v; }
    void set_flap_strength(float v) { flap_str_   = v; }
    void set_pipe_speed(float v)    { pipe_speed_ = v; }
    void set_gap_size(int v)        { gap_size_   = v; }

    bool is_active() const { return state_ != FBState::IDLE; }

    void start_game();
    void stop_game();
    void toggle_game();
    void flap();
    void game_tick();

 protected:
    float gravity_    { 0.45f };
    float flap_str_   { -7.5f };
    float pipe_speed_ { 2.5f  };
    int   gap_size_   { 72    };

    lv_obj_t   *canvas_ { nullptr };
    lv_timer_t *timer_  { nullptr };
    uint16_t   *fb_     { nullptr };  // direct pixel buffer (RGB565)

    FBState state_       { FBState::IDLE };
    float   bird_y_      { FB_H / 2.0f  };
    float   bird_vel_    { 0.0f         };
    int     bird_frame_  { 0            };
    int     frame_cnt_   { 0            };
    int     score_       { 0            };
    int     death_ticks_ { 0            };

    FBPipe  pipes_[FB_NPIPES] {};
    FBCloud clouds_[3]        {};

    void init_();
    void cleanup_();
    void reset_();
    void tick_playing_();
    void tick_dead_();
    bool check_collision_();

    // Direct-buffer drawing (fast)
    void render_();
    void fb_fill(int x, int y, int w, int h, uint16_t c);
    void fb_blit_alpha(int x, int y, const uint8_t *px, int sw, int sh);
    void fb_blend_rect(int x, int y, int w, int h, uint8_t opa);

    static void timer_cb_(lv_timer_t *);
};

template<typename... Ts>
class FlappyBirdStartAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public: void play(Ts... x) override { this->parent_->start_game(); }
};
template<typename... Ts>
class FlappyBirdStopAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public: void play(Ts... x) override { this->parent_->stop_game(); }
};
template<typename... Ts>
class FlappyBirdToggleAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public: void play(Ts... x) override { this->parent_->toggle_game(); }
};
template<typename... Ts>
class FlappyBirdFlapAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public: void play(Ts... x) override { this->parent_->flap(); }
};

}  // namespace flappy_bird
}  // namespace esphome

#endif  // USE_LVGL
