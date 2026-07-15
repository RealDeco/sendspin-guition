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
static const int FB_BIRD_R = 12;   // collision radius
static const int FB_NPIPES = 2;
static const int FB_GROUND = 22;
static const int FB_CAP_H  = 16;   // pipe cap sprite height

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

    lv_obj_t      *canvas_ { nullptr };
    lv_timer_t    *timer_  { nullptr };
    uint8_t       *buf_    { nullptr };

    // sprite descriptors (initialised at runtime)
    lv_image_dsc_t bird_sprs_[3] {};
    lv_image_dsc_t pipe_cap_up_  {};
    lv_image_dsc_t pipe_cap_dn_  {};

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
    void render_();
    void draw_bg_(lv_layer_t *l);
    void draw_pipes_(lv_layer_t *l);
    void draw_ground_(lv_layer_t *l);
    void draw_bird_(lv_layer_t *l);
    void draw_score_(lv_layer_t *l);
    void draw_game_over_(lv_layer_t *l);

    static void timer_cb_(lv_timer_t *t);
};

template<typename... Ts>
class FlappyBirdStartAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public:
    void play(Ts... x) override { this->parent_->start_game(); }
};
template<typename... Ts>
class FlappyBirdStopAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public:
    void play(Ts... x) override { this->parent_->stop_game(); }
};
template<typename... Ts>
class FlappyBirdToggleAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public:
    void play(Ts... x) override { this->parent_->toggle_game(); }
};
template<typename... Ts>
class FlappyBirdFlapAction : public Action<Ts...>, public Parented<FlappyBirdGame> {
 public:
    void play(Ts... x) override { this->parent_->flap(); }
};

}  // namespace flappy_bird
}  // namespace esphome

#endif  // USE_LVGL
