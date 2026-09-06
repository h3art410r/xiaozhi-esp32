// BOX0 PC simulator: runs the same game code as the ESP32-S3 firmware
// (box0_games.h from the xiaozhi-esp32 repo) on a 240x240 LVGL SDL window.
//
// Keys: Left/Right = switch menu item, Enter = M button, Esc = long-press LEFT (back)

#include <cstdlib>
#include <ctime>
#include <lvgl.h>
#include "drivers/sdl/lv_sdl_window.h"
#include "drivers/sdl/lv_sdl_keyboard.h"
#include "box0_games.h"

static Box0GamePlatform platform;
static Box0Flappy flappy;
static Box0Dino dino;

static lv_obj_t* menu_layer = nullptr;
static lv_obj_t* menu_rows[2] = {nullptr, nullptr};
static int menu_index = 0;
static bool menu_visible = false;

static void SelectMenuItem(int index) {
    menu_index = index;
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_opa(menu_rows[i], i == menu_index ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_t* label = lv_obj_get_child(menu_rows[i], 0);
        lv_obj_set_style_text_color(label, i == menu_index ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x9AA4AD), 0);
    }
}

static void ShowMenu() {
    if (menu_visible) {
        return;
    }
    menu_layer = lv_obj_create(lv_layer_top());
    lv_obj_set_size(menu_layer, 240, 240);
    lv_obj_set_pos(menu_layer, 0, 0);
    lv_obj_set_style_bg_color(menu_layer, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(menu_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menu_layer, 0, 0);
    lv_obj_set_style_radius(menu_layer, 0, 0);
    lv_obj_set_style_pad_all(menu_layer, 0, 0);
    lv_obj_remove_flag(menu_layer, LV_OBJ_FLAG_SCROLLABLE);

    const char* texts[2] = {"Flappy Bird", "Dino Run"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t* row = lv_obj_create(menu_layer);
        lv_obj_set_size(row, 240, 40);
        lv_obj_set_pos(row, 0, 60 + i * 48);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2F6FED), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* label = lv_label_create(row);
        lv_label_set_text(label, texts[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 22, 0);
        menu_rows[i] = row;
    }
    SelectMenuItem(menu_index);
    menu_visible = true;
}

static void HideMenu() {
    if (!menu_visible) {
        return;
    }
    lv_obj_delete(menu_layer);
    menu_layer = nullptr;
    menu_rows[0] = nullptr;
    menu_rows[1] = nullptr;
    menu_visible = false;
}

static void OnKey(lv_event_t* e) {
    uint32_t key = lv_event_get_key(e);
    if (flappy.IsActive()) {
        if (key == LV_KEY_ENTER) {
            flappy.Flap();
        } else if (key == LV_KEY_ESC) {
            flappy.Stop();
            ShowMenu();
        }
        return;
    }
    if (dino.IsActive()) {
        if (key == LV_KEY_ENTER) {
            dino.Jump();
        } else if (key == LV_KEY_ESC) {
            dino.Stop();
            ShowMenu();
        }
        return;
    }
    if (menu_visible) {
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            SelectMenuItem(1 - menu_index);
        } else if (key == LV_KEY_ENTER) {
            int sel = menu_index;
            HideMenu();
            if (sel == 0) {
                flappy.Start();
            } else {
                dino.Start();
            }
        }
        return;
    }
    ShowMenu();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    srand((unsigned)time(nullptr));

    lv_init();
    lv_display_t* disp = lv_sdl_window_create(240, 240);
    lv_sdl_window_set_title(disp, "BOX0 Sim | L/R: switch  Enter: M  Esc: back");
    lv_sdl_window_set_zoom(disp, 3.0f);

    lv_indev_t* kb = lv_sdl_keyboard_create();
    lv_group_t* group = lv_group_create();
    lv_group_add_obj(group, lv_screen_active());
    lv_indev_set_group(kb, group);
    lv_group_focus_obj(lv_screen_active());
    lv_obj_add_event_cb(lv_screen_active(), OnKey, LV_EVENT_KEY, nullptr);

    platform.random = []() { return (uint32_t)rand(); };
    platform.get_font = []() -> const lv_font_t* { return &lv_font_montserrat_24; };
    flappy.SetPlatform(platform);
    dino.SetPlatform(platform);

    ShowMenu();

    while (true) {
        lv_timer_handler();
        lv_delay_ms(5);
    }
    return 0;
}
