#pragma once

#include "esphome/core/defines.h"
#ifdef USE_LVGL

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "lvgl.h"

namespace esphome {
namespace plane_radar {

static constexpr int PR_W   = 240;
static constexpr int PR_H   = 240;
static constexpr int PR_CX  = 120;
static constexpr int PR_CY  = 120;
static constexpr int PR_R   = 118;
static constexpr int MAX_AC = 64;

static constexpr uint16_t pr_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static constexpr uint16_t PR_COL_BG       = pr_rgb(6,  18, 10);
static constexpr uint16_t PR_COL_BORDER   = pr_rgb(0,  45, 22);
static constexpr uint16_t PR_COL_RING     = pr_rgb(0,  70, 35);
static constexpr uint16_t PR_COL_RING_DIM = pr_rgb(0,  45, 22);
static constexpr uint16_t PR_COL_AIRCRAFT = pr_rgb(50, 255, 120);
static constexpr uint16_t PR_COL_EDGE_AC  = pr_rgb(0,  130, 60);
static constexpr uint16_t PR_COL_NORTH    = pr_rgb(220, 80, 80);
static constexpr uint16_t PR_COL_CENTER   = pr_rgb(160, 255, 160);

static constexpr int RANGE_PRESETS[]  = {10, 25, 50, 100, 200, 500};
static constexpr int N_RANGE_PRESETS  = 6;

struct Aircraft {
    float lat, lon;
    float track_deg;
    float gs_knots;
    int   alt_baro;
    char  callsign[9];
    bool  valid;
};

enum class PRState { IDLE, ACTIVE };

class PlaneRadarAircraftUpdateTrigger : public Trigger<int> {};

class PlaneRadar : public Component {
 public:
    void setup() override {}
    void loop() override;
    float get_setup_priority() const override { return setup_priority::LATE; }

    void set_latitude(float v)         { center_lat_ = v; }
    void set_longitude(float v)        { center_lon_ = v; }
    void set_update_interval(int v)    { update_interval_ms_ = v; }
    void set_initial_range_km(int v)   { range_km_ = v; }
    void set_use_miles(bool v)         { use_miles_ = v; }

    void register_aircraft_update_trigger(PlaneRadarAircraftUpdateTrigger *t) {
        aircraft_update_trigger_ = t;
    }

    bool is_active() const { return state_ != PRState::IDLE; }

    void start();
    void stop();
    void toggle();
    void cycle_range();

 protected:
    float center_lat_        { 0.0f  };
    float center_lon_        { 0.0f  };
    int   update_interval_ms_{ 15000 };
    int   range_km_          { 50    };
    bool  use_miles_         { false };

    PRState   state_  { PRState::IDLE };
    lv_obj_t *canvas_ { nullptr };
    uint16_t *fb_     { nullptr };

    Aircraft aircraft_[MAX_AC] {};
    int      aircraft_count_   { 0 };

    uint32_t last_fetch_ms_  { 0 };
    uint32_t last_render_ms_ { 0 };

    PlaneRadarAircraftUpdateTrigger *aircraft_update_trigger_ { nullptr };

    void init_();
    void cleanup_();
    void fetch_();
    void render_();
    void geo_to_pixel_(float lat, float lon, int &px, int &py);

    inline void set_pixel_(int x, int y, uint16_t c) {
        if ((unsigned)x < PR_W && (unsigned)y < PR_H)
            fb_[y * PR_W + x] = c;
    }
    void fill_rect_(int x, int y, int w, int h, uint16_t c);
    void draw_circle_(int cx, int cy, int r, uint16_t c);
    void fill_circle_(int cx, int cy, int r, uint16_t c);
    void fill_triangle_(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
    void draw_aircraft_(int cx, int cy, float heading_deg);
    void draw_char_(int x, int y, char ch, uint16_t c, int scale = 1);
    void draw_text_(int x, int y, const char *text, uint16_t c, int scale = 1);
};

template<typename... Ts>
class PlaneRadarStartAction : public Action<Ts...>, public Parented<PlaneRadar> {
 public: void play(Ts... x) override { this->parent_->start(); }
};
template<typename... Ts>
class PlaneRadarStopAction : public Action<Ts...>, public Parented<PlaneRadar> {
 public: void play(Ts... x) override { this->parent_->stop(); }
};
template<typename... Ts>
class PlaneRadarToggleAction : public Action<Ts...>, public Parented<PlaneRadar> {
 public: void play(Ts... x) override { this->parent_->toggle(); }
};
template<typename... Ts>
class PlaneRadarCycleRangeAction : public Action<Ts...>, public Parented<PlaneRadar> {
 public: void play(Ts... x) override { this->parent_->cycle_range(); }
};

}  // namespace plane_radar
}  // namespace esphome

#endif  // USE_LVGL
