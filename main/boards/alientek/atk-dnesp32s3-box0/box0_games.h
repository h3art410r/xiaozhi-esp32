#pragma once

// Platform-neutral Flappy Bird and Dino Run games for the ATK BOX0 240x240 LVGL screen.
// Shared between the ESP32-S3 firmware (atk_dnesp32s3_box0.cc) and the PC simulator
// (box0-sim) so gameplay tweaks only need to happen in one place.

#include <lvgl.h>
#include <cstdint>
#include <functional>

struct Box0GamePlatform {
    std::function<uint32_t()> random;            // esp_random() on device, rand() in sim
    std::function<const lv_font_t*()> get_font;  // theme font on device, sim font in sim
    std::function<void()> lock;                  // display mutex on device, unused in sim
    std::function<void()> unlock;
};

namespace box0_game_detail {

class LockGuard {
public:
    explicit LockGuard(const Box0GamePlatform& platform) : platform_(platform) {
        if (platform_.lock) {
            platform_.lock();
        }
    }
    ~LockGuard() {
        if (platform_.unlock) {
            platform_.unlock();
        }
    }

private:
    const Box0GamePlatform& platform_;
};

inline uint32_t Random(const Box0GamePlatform& platform) {
    return platform.random ? platform.random() : 4;  // chosen by fair dice roll
}

inline const lv_font_t* Font(const Box0GamePlatform& platform) {
    return platform.get_font ? platform.get_font() : nullptr;
}

}  // namespace box0_game_detail

// ---------------- Flappy Bird ----------------
class Box0Flappy {
public:
    void SetPlatform(const Box0GamePlatform& platform) { platform_ = platform; }
    bool IsActive() const { return active_; }

    void Start() {
        if (active_) {
            return;
        }
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        int h = lv_display_get_vertical_resolution(disp);
        const lv_font_t* font = box0_game_detail::Font(platform_);

        {
            box0_game_detail::LockGuard lock(platform_);
            layer_ = lv_obj_create(lv_layer_top());
            lv_obj_set_size(layer_, w, h);
            lv_obj_set_pos(layer_, 0, 0);
            lv_obj_set_style_bg_color(layer_, lv_color_hex(0x4EC0CA), 0);
            lv_obj_set_style_bg_opa(layer_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(layer_, 0, 0);
            lv_obj_set_style_radius(layer_, 0, 0);
            lv_obj_set_style_pad_all(layer_, 0, 0);
            lv_obj_remove_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);

            // Ground strip with grass top, like the original
            lv_obj_t* ground = lv_obj_create(layer_);
            lv_obj_set_size(ground, w, kGroundH);
            lv_obj_set_pos(ground, 0, h - kGroundH);
            lv_obj_set_style_bg_color(ground, lv_color_hex(0xDED895), 0);
            lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(ground, 0, 0);
            lv_obj_set_style_radius(ground, 0, 0);
            lv_obj_set_style_pad_all(ground, 0, 0);
            lv_obj_remove_flag(ground, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* grass = lv_obj_create(layer_);
            lv_obj_set_size(grass, w, 5);
            lv_obj_set_pos(grass, 0, h - kGroundH);
            lv_obj_set_style_bg_color(grass, lv_color_hex(0x9EE37D), 0);
            lv_obj_set_style_bg_opa(grass, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(grass, 0, 0);
            lv_obj_set_style_radius(grass, 0, 0);
            lv_obj_set_style_pad_all(grass, 0, 0);
            lv_obj_remove_flag(grass, LV_OBJ_FLAG_SCROLLABLE);

            // Pipes first so bird and score render on top of them
            for (int i = 0; i < 4; i++) {
                pipe_top_[i] = lv_obj_create(layer_);
                pipe_bottom_[i] = lv_obj_create(layer_);
                pipe_cap_top_[i] = lv_obj_create(layer_);
                pipe_cap_bottom_[i] = lv_obj_create(layer_);
                StylePipe(pipe_top_[i]);
                StylePipe(pipe_bottom_[i]);
                StylePipe(pipe_cap_top_[i]);
                StylePipe(pipe_cap_bottom_[i]);
            }

            // Bird: yellow body with orange beak and eye, tilts with velocity
            bird_ = lv_obj_create(layer_);
            lv_obj_set_size(bird_, kBirdW, kBirdH);
            lv_obj_set_style_radius(bird_, 7, 0);
            lv_obj_set_style_bg_color(bird_, lv_color_hex(0xFBD343), 0);
            lv_obj_set_style_bg_opa(bird_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bird_, 0, 0);
            lv_obj_set_style_pad_all(bird_, 0, 0);
            lv_obj_remove_flag(bird_, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* beak = lv_obj_create(bird_);
            lv_obj_set_size(beak, 7, 5);
            lv_obj_set_pos(beak, kBirdW - 4, 5);
            lv_obj_set_style_radius(beak, 2, 0);
            lv_obj_set_style_bg_color(beak, lv_color_hex(0xF8862E), 0);
            lv_obj_set_style_bg_opa(beak, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(beak, 0, 0);
            lv_obj_remove_flag(beak, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* eye = lv_obj_create(bird_);
            lv_obj_set_size(eye, 6, 6);
            lv_obj_set_pos(eye, kBirdW - 9, 1);
            lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(eye, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(eye, 0, 0);
            lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* pupil = lv_obj_create(bird_);
            lv_obj_set_size(pupil, 3, 3);
            lv_obj_set_pos(pupil, kBirdW - 6, 2);
            lv_obj_set_style_radius(pupil, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(pupil, lv_color_hex(0x202020), 0);
            lv_obj_set_style_bg_opa(pupil, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pupil, 0, 0);
            lv_obj_remove_flag(pupil, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_set_style_transform_pivot_x(bird_, kBirdW / 2, 0);
            lv_obj_set_style_transform_pivot_y(bird_, kBirdH / 2, 0);

            score_label_ = lv_label_create(layer_);
            lv_obj_set_style_text_color(score_label_, lv_color_hex(0xFFFFFF), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(score_label_, font, 0);
            }
            lv_obj_align(score_label_, LV_ALIGN_TOP_MID, 0, 16);

            over_label_ = lv_label_create(layer_);
            lv_obj_set_style_text_color(over_label_, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_align(over_label_, LV_TEXT_ALIGN_CENTER, 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(over_label_, font, 0);
            }
            lv_obj_align(over_label_, LV_ALIGN_CENTER, 0, -10);
            lv_obj_add_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
        }

        timer_ = lv_timer_create(TickCb, 20, this);
        Reset();
        active_ = true;
    }

    void Stop() {
        if (!active_) {
            return;
        }
        active_ = false;
        if (timer_ != nullptr) {
            lv_timer_delete(timer_);
            timer_ = nullptr;
        }
        box0_game_detail::LockGuard lock(platform_);
        lv_obj_delete(layer_);
        layer_ = nullptr;
        bird_ = nullptr;
        for (int i = 0; i < 4; i++) {
            pipe_top_[i] = nullptr;
            pipe_bottom_[i] = nullptr;
            pipe_cap_top_[i] = nullptr;
            pipe_cap_bottom_[i] = nullptr;
        }
    }

    void Flap() {
        if (!active_) {
            return;
        }
        if (over_) {
            box0_game_detail::LockGuard lock(platform_);
            Reset();
            return;
        }
        bird_vy_ = -5.3f;
    }

private:
    static constexpr int kBirdX = 52;
    static constexpr int kBirdW = 18;
    static constexpr int kBirdH = 14;
    static constexpr int kPipeW = 34;
    static constexpr int kPipeCapH = 16;
    static constexpr int kPipeGap = 82;
    static constexpr int kPipeSpacing = 135;
    static constexpr int kGroundH = 26;

    Box0GamePlatform platform_;
    lv_obj_t* layer_ = nullptr;
    lv_obj_t* bird_ = nullptr;
    lv_obj_t* score_label_ = nullptr;
    lv_obj_t* over_label_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_obj_t* pipe_top_[4] = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* pipe_bottom_[4] = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* pipe_cap_top_[4] = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* pipe_cap_bottom_[4] = {nullptr, nullptr, nullptr, nullptr};
    float pipe_x_[4] = {0, 0, 0, 0};
    float pipe_gap_y_[4] = {0, 0, 0, 0};
    bool pipe_active_[4] = {false, false, false, false};
    bool pipe_scored_[4] = {false, false, false, false};
    float bird_y_ = 0;
    float bird_vy_ = 0;
    int score_ = 0;
    bool active_ = false;
    bool over_ = false;

    static void TickCb(lv_timer_t* timer) {
        auto* self = static_cast<Box0Flappy*>(lv_timer_get_user_data(timer));
        self->Update();
    }

    static void StylePipe(lv_obj_t* p) {
        lv_obj_set_style_bg_color(p, lv_color_hex(0x74BF2E), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(p, 2, 0);
        lv_obj_set_style_border_color(p, lv_color_hex(0x3F7A1E), 0);
        lv_obj_set_style_radius(p, 0, 0);
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }

    void Reset() {
        auto disp = lv_display_get_default();
        int h = lv_display_get_vertical_resolution(disp);
        bird_y_ = (h - kGroundH) / 2.0f;
        bird_vy_ = 0;
        score_ = 0;
        over_ = false;
        lv_label_set_text(score_label_, "0");
        lv_obj_add_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) {
            pipe_active_[i] = false;
            pipe_scored_[i] = false;
            lv_obj_add_flag(pipe_top_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pipe_bottom_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pipe_cap_top_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pipe_cap_bottom_[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_transform_angle(bird_, 0, 0);
        lv_obj_set_pos(bird_, kBirdX, (int)bird_y_);
    }

    void Update() {
        if (!active_ || over_) {
            return;
        }
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        int h = lv_display_get_vertical_resolution(disp);
        int ground_top = h - kGroundH;
        float speed = 2.6f + (score_ < 20 ? score_ : 20) * 0.03f;

        // Snappy classic feel: strong flap, quick fall with terminal velocity
        bird_vy_ += 0.34f;
        if (bird_vy_ > 6.5f) {
            bird_vy_ = 6.5f;
        }
        bird_y_ += bird_vy_;
        if (bird_y_ < 0) {  // ceiling just stops the bird; only ground and pipes kill
            bird_y_ = 0;
            bird_vy_ = 0;
        }
        int angle = (int)(bird_vy_ * 90.0f);
        if (angle < -250) {
            angle = -250;
        }
        if (angle > 800) {
            angle = 800;
        }
        lv_obj_set_style_transform_angle(bird_, angle, 0);
        lv_obj_set_pos(bird_, kBirdX, (int)bird_y_);

        for (int i = 0; i < 4; i++) {
            if (!pipe_active_[i]) {
                continue;
            }
            pipe_x_[i] -= speed;
            int px = (int)pipe_x_[i];
            int gap = (int)pipe_gap_y_[i];
            lv_obj_set_size(pipe_top_[i], kPipeW, gap);
            lv_obj_set_pos(pipe_top_[i], px, 0);
            lv_obj_set_size(pipe_cap_top_[i], kPipeW + 6, kPipeCapH);
            lv_obj_set_pos(pipe_cap_top_[i], px - 3, gap - kPipeCapH);
            int by = gap + kPipeGap;
            lv_obj_set_size(pipe_bottom_[i], kPipeW, ground_top - by);
            lv_obj_set_pos(pipe_bottom_[i], px, by);
            lv_obj_set_size(pipe_cap_bottom_[i], kPipeW + 6, kPipeCapH);
            lv_obj_set_pos(pipe_cap_bottom_[i], px - 3, by);

            if (!pipe_scored_[i] && pipe_x_[i] + kPipeW < kBirdX) {
                pipe_scored_[i] = true;
                score_++;
                lv_label_set_text_fmt(score_label_, "%d", score_);
            }
            if (pipe_x_[i] < -(kPipeW + 8)) {
                pipe_active_[i] = false;
                lv_obj_add_flag(pipe_top_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(pipe_bottom_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(pipe_cap_top_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(pipe_cap_bottom_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        bool need_spawn = true;
        for (int i = 0; i < 4; i++) {
            if (pipe_active_[i] && pipe_x_[i] > w - kPipeSpacing) {
                need_spawn = false;
                break;
            }
        }
        if (need_spawn) {
            for (int i = 0; i < 4; i++) {
                if (!pipe_active_[i]) {
                    pipe_active_[i] = true;
                    pipe_scored_[i] = false;
                    pipe_x_[i] = w;
                    pipe_gap_y_[i] = 24 + box0_game_detail::Random(platform_) % (uint32_t)(ground_top - kPipeGap - 48);
                    lv_obj_remove_flag(pipe_top_[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(pipe_bottom_[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(pipe_cap_top_[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(pipe_cap_bottom_[i], LV_OBJ_FLAG_HIDDEN);
                    break;
                }
            }
        }

        // Collision with small forgiveness margin for better feel
        bool hit = (bird_y_ + kBirdH > ground_top);
        if (!hit) {
            for (int i = 0; i < 4; i++) {
                if (!pipe_active_[i]) {
                    continue;
                }
                if (kBirdX + kBirdW - 2 > pipe_x_[i] && kBirdX + 2 < pipe_x_[i] + kPipeW) {
                    if (bird_y_ + 2 < pipe_gap_y_[i] || bird_y_ + kBirdH - 2 > pipe_gap_y_[i] + kPipeGap) {
                        hit = true;
                        break;
                    }
                }
            }
        }
        if (hit) {
            over_ = true;
            lv_label_set_text_fmt(over_label_, "Game Over\nScore: %d\nPress M to retry", score_);
            lv_obj_remove_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
};

// ---------------- Dino Run ----------------
class Box0Dino {
public:
    void SetPlatform(const Box0GamePlatform& platform) { platform_ = platform; }
    bool IsActive() const { return active_; }

    void Start() {
        if (active_) {
            return;
        }
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        int h = lv_display_get_vertical_resolution(disp);
        const lv_font_t* font = box0_game_detail::Font(platform_);

        {
            box0_game_detail::LockGuard lock(platform_);
            layer_ = lv_obj_create(lv_layer_top());
            lv_obj_set_size(layer_, w, h);
            lv_obj_set_pos(layer_, 0, 0);
            lv_obj_set_style_bg_color(layer_, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(layer_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(layer_, 0, 0);
            lv_obj_set_style_radius(layer_, 0, 0);
            lv_obj_set_style_pad_all(layer_, 0, 0);
            lv_obj_remove_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);

            // Drifting clouds (parallax background)
            for (int i = 0; i < 2; i++) {
                cloud_[i] = lv_obj_create(layer_);
                lv_obj_set_size(cloud_[i], 24, 9);
                lv_obj_set_style_radius(cloud_[i], 4, 0);
                lv_obj_set_style_bg_color(cloud_[i], lv_color_hex(0xE4E4E4), 0);
                lv_obj_set_style_bg_opa(cloud_[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(cloud_[i], 0, 0);
                lv_obj_set_style_pad_all(cloud_[i], 0, 0);
                lv_obj_remove_flag(cloud_[i], LV_OBJ_FLAG_SCROLLABLE);
            }

            // Ground line
            lv_obj_t* ground = lv_obj_create(layer_);
            lv_obj_set_size(ground, w, 2);
            lv_obj_set_pos(ground, 0, h - kGroundH);
            lv_obj_set_style_bg_color(ground, lv_color_hex(0x535353), 0);
            lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(ground, 0, 0);
            lv_obj_set_style_radius(ground, 0, 0);
            lv_obj_set_style_pad_all(ground, 0, 0);
            lv_obj_remove_flag(ground, LV_OBJ_FLAG_SCROLLABLE);

            // Scrolling ground dashes for speed feedback
            for (int i = 0; i < 6; i++) {
                dash_[i] = lv_obj_create(layer_);
                lv_obj_set_size(dash_[i], 10, 2);
                lv_obj_set_style_radius(dash_[i], 0, 0);
                lv_obj_set_style_bg_color(dash_[i], lv_color_hex(0xC9C9C9), 0);
                lv_obj_set_style_bg_opa(dash_[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(dash_[i], 0, 0);
                lv_obj_set_style_pad_all(dash_[i], 0, 0);
                lv_obj_remove_flag(dash_[i], LV_OBJ_FLAG_SCROLLABLE);
            }

            for (int i = 0; i < 4; i++) {
                cactus_[i] = lv_obj_create(layer_);
                lv_obj_set_style_bg_color(cactus_[i], lv_color_hex(0x535353), 0);
                lv_obj_set_style_bg_opa(cactus_[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(cactus_[i], 0, 0);
                lv_obj_set_style_radius(cactus_[i], 2, 0);
                lv_obj_set_style_pad_all(cactus_[i], 0, 0);
                lv_obj_remove_flag(cactus_[i], LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(cactus_[i], LV_OBJ_FLAG_HIDDEN);
            }

            // Dino: transparent container holding body, head, eye and legs
            dino_ = lv_obj_create(layer_);
            lv_obj_set_size(dino_, kDinoW, kDinoH);
            lv_obj_set_style_bg_opa(dino_, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(dino_, 0, 0);
            lv_obj_set_style_radius(dino_, 0, 0);
            lv_obj_set_style_pad_all(dino_, 0, 0);
            lv_obj_remove_flag(dino_, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* body = lv_obj_create(dino_);
            lv_obj_set_size(body, 16, 17);
            lv_obj_set_pos(body, 0, 9);
            lv_obj_set_style_radius(body, 3, 0);
            lv_obj_set_style_bg_color(body, lv_color_hex(0x535353), 0);
            lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(body, 0, 0);
            lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* head = lv_obj_create(dino_);
            lv_obj_set_size(head, 13, 11);
            lv_obj_set_pos(head, 10, 0);
            lv_obj_set_style_radius(head, 3, 0);
            lv_obj_set_style_bg_color(head, lv_color_hex(0x535353), 0);
            lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(head, 0, 0);
            lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* eye = lv_obj_create(dino_);
            lv_obj_set_size(eye, 3, 3);
            lv_obj_set_pos(eye, 17, 3);
            lv_obj_set_style_radius(eye, 0, 0);
            lv_obj_set_style_bg_color(eye, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(eye, 0, 0);
            lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);

            leg_a_ = lv_obj_create(dino_);
            lv_obj_set_size(leg_a_, 5, 5);
            lv_obj_set_pos(leg_a_, 4, 21);
            lv_obj_set_style_radius(leg_a_, 0, 0);
            lv_obj_set_style_bg_color(leg_a_, lv_color_hex(0x535353), 0);
            lv_obj_set_style_bg_opa(leg_a_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(leg_a_, 0, 0);
            lv_obj_remove_flag(leg_a_, LV_OBJ_FLAG_SCROLLABLE);

            leg_b_ = lv_obj_create(dino_);
            lv_obj_set_size(leg_b_, 5, 5);
            lv_obj_set_pos(leg_b_, 14, 21);
            lv_obj_set_style_radius(leg_b_, 0, 0);
            lv_obj_set_style_bg_color(leg_b_, lv_color_hex(0x535353), 0);
            lv_obj_set_style_bg_opa(leg_b_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(leg_b_, 0, 0);
            lv_obj_remove_flag(leg_b_, LV_OBJ_FLAG_SCROLLABLE);

            score_label_ = lv_label_create(layer_);
            lv_obj_set_style_text_color(score_label_, lv_color_hex(0x535353), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(score_label_, font, 0);
            }
            lv_obj_align(score_label_, LV_ALIGN_TOP_RIGHT, -10, 8);

            over_label_ = lv_label_create(layer_);
            lv_obj_set_style_text_color(over_label_, lv_color_hex(0x535353), 0);
            lv_obj_set_style_text_align(over_label_, LV_TEXT_ALIGN_CENTER, 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(over_label_, font, 0);
            }
            lv_obj_align(over_label_, LV_ALIGN_CENTER, 0, -24);
            lv_obj_add_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
        }

        timer_ = lv_timer_create(TickCb, 20, this);
        Reset();
        active_ = true;
    }

    void Stop() {
        if (!active_) {
            return;
        }
        active_ = false;
        if (timer_ != nullptr) {
            lv_timer_delete(timer_);
            timer_ = nullptr;
        }
        box0_game_detail::LockGuard lock(platform_);
        lv_obj_delete(layer_);
        layer_ = nullptr;
        dino_ = nullptr;
        leg_a_ = nullptr;
        leg_b_ = nullptr;
        for (int i = 0; i < 4; i++) {
            cactus_[i] = nullptr;
        }
        for (int i = 0; i < 2; i++) {
            cloud_[i] = nullptr;
        }
        for (int i = 0; i < 6; i++) {
            dash_[i] = nullptr;
        }
    }

    void Jump() {
        if (!active_) {
            return;
        }
        if (over_) {
            box0_game_detail::LockGuard lock(platform_);
            Reset();
            return;
        }
        if (on_ground_) {
            vy_ = -6.4f;
            on_ground_ = false;
            lv_obj_set_pos(leg_a_, 4, 21);
            lv_obj_set_pos(leg_b_, 14, 21);
        }
    }

private:
    static constexpr int kDinoX = 30;
    static constexpr int kDinoW = 24;
    static constexpr int kDinoH = 26;
    static constexpr int kGroundH = 20;

    Box0GamePlatform platform_;
    lv_obj_t* layer_ = nullptr;
    lv_obj_t* dino_ = nullptr;
    lv_obj_t* score_label_ = nullptr;
    lv_obj_t* over_label_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_obj_t* cactus_[4] = {nullptr, nullptr, nullptr, nullptr};
    float cactus_x_[4] = {0, 0, 0, 0};
    int cactus_h_[4] = {0, 0, 0, 0};
    int cactus_w_[4] = {0, 0, 0, 0};
    bool cactus_active_[4] = {false, false, false, false};
    float y_ = 0;
    float vy_ = 0;
    bool on_ground_ = true;
    int score_ = 0;
    int frame_ = 0;
    int spawn_gap_ = 220;
    bool active_ = false;
    bool over_ = false;
    lv_obj_t* leg_a_ = nullptr;
    lv_obj_t* leg_b_ = nullptr;
    lv_obj_t* cloud_[2] = {nullptr, nullptr};
    float cloud_x_[2] = {0, 0};
    lv_obj_t* dash_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    float dash_x_[6] = {0, 0, 0, 0, 0, 0};

    static void TickCb(lv_timer_t* timer) {
        auto* self = static_cast<Box0Dino*>(lv_timer_get_user_data(timer));
        self->Update();
    }

    void Reset() {
        auto disp = lv_display_get_default();
        int h = lv_display_get_vertical_resolution(disp);
        y_ = h - kGroundH - kDinoH;
        vy_ = 0;
        on_ground_ = true;
        score_ = 0;
        frame_ = 0;
        over_ = false;
        spawn_gap_ = 220;
        lv_label_set_text(score_label_, "00000");
        lv_obj_add_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) {
            cactus_active_[i] = false;
            lv_obj_add_flag(cactus_[i], LV_OBJ_FLAG_HIDDEN);
        }
        cloud_x_[0] = 70;
        cloud_x_[1] = 180;
        lv_obj_set_pos(cloud_[0], 70, 32);
        lv_obj_set_pos(cloud_[1], 180, 74);
        for (int i = 0; i < 6; i++) {
            dash_x_[i] = 20 + i * 55;
            lv_obj_set_pos(dash_[i], (int)dash_x_[i], h - kGroundH + 8);
        }
        lv_obj_set_pos(leg_a_, 4, 21);
        lv_obj_set_pos(leg_b_, 14, 21);
        lv_obj_set_pos(dino_, kDinoX, (int)y_);
    }

    void Update() {
        if (!active_ || over_) {
            return;
        }
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        int h = lv_display_get_vertical_resolution(disp);
        float ground_top = h - kGroundH;
        // Speed ramps up with score like the original (6 -> 13 px/frame at 60fps)
        float speed = 3.4f + score_ * 0.006f;
        if (speed > 6.0f) {
            speed = 6.0f;
        }

        // Jump physics: quick rise, quick fall
        if (!on_ground_) {
            vy_ += 0.42f;
            y_ += vy_;
            float rest = ground_top - kDinoH;
            if (y_ >= rest) {
                y_ = rest;
                vy_ = 0;
                on_ground_ = true;
            }
            lv_obj_set_pos(dino_, kDinoX, (int)y_);
        } else if (frame_ % 6 == 0) {
            // Running legs animation
            bool up = (frame_ / 6) % 2 == 0;
            lv_obj_set_pos(leg_a_, 4, up ? 19 : 21);
            lv_obj_set_pos(leg_b_, 14, up ? 21 : 19);
        }

        // Parallax clouds
        for (int i = 0; i < 2; i++) {
            cloud_x_[i] -= speed * 0.35f;
            if (cloud_x_[i] < -26) {
                cloud_x_[i] = w + 20 + box0_game_detail::Random(platform_) % 60;
            }
            lv_obj_set_pos(cloud_[i], (int)cloud_x_[i], i == 0 ? 32 : 74);
        }

        // Ground dashes scroll at full speed
        for (int i = 0; i < 6; i++) {
            dash_x_[i] -= speed;
            if (dash_x_[i] < -12) {
                dash_x_[i] += 6 * 55;
            }
            lv_obj_set_pos(dash_[i], (int)dash_x_[i], h - kGroundH + 8);
        }

        // Move cacti
        for (int i = 0; i < 4; i++) {
            if (!cactus_active_[i]) {
                continue;
            }
            cactus_x_[i] -= speed;
            lv_obj_set_size(cactus_[i], cactus_w_[i], cactus_h_[i]);
            lv_obj_set_pos(cactus_[i], (int)cactus_x_[i], (int)(ground_top - cactus_h_[i]));
            if (cactus_x_[i] < -20) {
                cactus_active_[i] = false;
                lv_obj_add_flag(cactus_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Spawn cactus; gap scales with speed so jumps stay doable
        bool need_spawn = true;
        for (int i = 0; i < 4; i++) {
            if (cactus_active_[i] && cactus_x_[i] > w - spawn_gap_) {
                need_spawn = false;
                break;
            }
        }
        if (need_spawn) {
            for (int i = 0; i < 4; i++) {
                if (!cactus_active_[i]) {
                    static const int kCactusW[3] = {12, 14, 16};
                    static const int kCactusH[3] = {24, 30, 36};
                    int kind = box0_game_detail::Random(platform_) % 3;
                    cactus_active_[i] = true;
                    cactus_x_[i] = w;
                    cactus_w_[i] = kCactusW[kind];
                    cactus_h_[i] = kCactusH[kind];
                    spawn_gap_ = (int)(170 + speed * 22) + box0_game_detail::Random(platform_) % 60;
                    lv_obj_remove_flag(cactus_[i], LV_OBJ_FLAG_HIDDEN);
                    break;
                }
            }
        }

        // Score by survival time
        frame_++;
        if (frame_ % 5 == 0) {
            score_++;
            lv_label_set_text_fmt(score_label_, "%05d", score_);
        }

        // Collision with small forgiveness margin
        bool hit = false;
        for (int i = 0; i < 4; i++) {
            if (!cactus_active_[i]) {
                continue;
            }
            if (kDinoX + kDinoW - 3 > cactus_x_[i] && kDinoX + 2 < cactus_x_[i] + cactus_w_[i]) {
                if (y_ + kDinoH > ground_top - cactus_h_[i] + 2) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit) {
            over_ = true;
            lv_label_set_text_fmt(over_label_, "G A M E  O V E R\nScore: %05d\nPress M to retry", score_);
            lv_obj_remove_flag(over_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
};
