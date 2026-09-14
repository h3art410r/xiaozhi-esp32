#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "power_manager.h"
#include "box0_games.h"
#include "box0_local_config.h"
#include <ssid_manager.h>

#include "i2c_device.h"
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <lvgl.h>
#include <esp_app_desc.h>
#include <esp_mac.h>
#include <esp_random.h>
#include "display/lvgl_display/lvgl_theme.h"
#include <esp_netif.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <lwip/ip4_addr.h>
#include <mdns.h>
#include <sys/time.h>
#include <errno.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <cstdlib>

#define TAG "atk_dnesp32s3_box0"

class atk_dnesp32s3_box0  : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button right_button_;   
    Button left_button_;    
    Button middle_button_;
    LcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    PowerSupply power_status_;
    LcdStatus LcdStatus_ = kDevicelcdbacklightOn;
    PowerSleep power_sleep_ = kDeviceNoSleep;
    WakeStatus wake_status_ = kDeviceAwakened;
    XiaozhiStatus XiaozhiStatus_ = kDevice_Exit_Distributionnetwork;
    esp_timer_handle_t wake_timer_handle_;
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    // Boot menu state (paged: 3 items per page, L/R wraps across pages)
    static constexpr int kMenuItemCount = 6;
    static constexpr int kMenuItemsPerPage = 3;
    lv_obj_t* menu_layer_ = nullptr;
    lv_obj_t* menu_items_[kMenuItemsPerPage] = {nullptr, nullptr, nullptr};
    int menu_index_ = 0;
    int menu_page_ = -1;
    lv_obj_t* menu_page_label_ = nullptr;
    bool menu_visible_ = false;
    lv_obj_t* about_layer_ = nullptr;
    lv_obj_t* about_body_label_ = nullptr;
    lv_obj_t* about_title_label_ = nullptr;
    int about_page_ = 0;
    static constexpr int kAboutPages = 2;
    // Power submenu state ("Power Off" / "Reboot"), layered over the boot menu
    lv_obj_t* power_layer_ = nullptr;
    lv_obj_t* power_items_[2] = {nullptr, nullptr};
    lv_obj_t* power_hint_label_ = nullptr;
    int power_index_ = 0;
    std::shared_ptr<LvglFont> menu_font_ = nullptr;
    esp_timer_handle_t menu_timer_ = nullptr;
    bool menu_pending_ = true;
    // Press-down navigation: menu/pages react on press, not on release; the
    // main menu also scrolls continuously while L/R is held.
    esp_timer_handle_t nav_repeat_timer_ = nullptr;
    int nav_repeat_dir_ = 0;
    uint32_t nav_repeat_ticks_ = 0;
    bool nav_press_consumed_ = false;
    bool m_press_consumed_ = false;

    // Games; logic lives in box0_games.h and is shared with the PC simulator
    Box0GamePlatform game_platform_;
    Box0Flappy flappy_game_;
    Box0Dino dino_game_;
    DisplayLockGuard* game_lock_ = nullptr;


    // Wireless mic (MicYou protocol) state
    lv_obj_t* mic_layer_ = nullptr;
    lv_obj_t* mic_status_label_ = nullptr;
    lv_obj_t* mic_pc_label_ = nullptr;
    TaskHandle_t mic_task_handle_ = nullptr;
    std::atomic<bool> mic_task_stop_{false};
    std::atomic<bool> mic_streaming_{false};
    bool mic_hold_ = false;
    bool mic_hold_consumed_ = false;
    esp_timer_handle_t mic_hold_timer_ = nullptr;
    bool mic_engine_muted_ = false;
    bool mdns_started_ = false;
    int64_t mic_session_id_ = 0;
    int32_t mic_seq_ = 0;
    uint8_t mic_rx_acc_[512];
    size_t mic_rx_len_ = 0;
    uint8_t mic_frame_buf_[800];
    esp_ip4_addr_t mic_pc_ip_{};
    std::atomic<int32_t> mic_level_{0};
    std::atomic<int64_t> mic_mute_until_ms_{0};
    std::atomic<int> mic_cue_request_{-1};
    TaskHandle_t mic_cue_task_handle_ = nullptr;
    std::atomic<bool> mic_cue_task_stop_{false};
    lv_obj_t* mic_bars_[20] = {};
    int mic_wave_hist_[20] = {};
    lv_timer_t* mic_wave_timer_ = nullptr;
    static constexpr int kMicVoicePort = 9125;
    // Continuous-recording + hotkey model: recording runs for the whole page
    // session (auto-start on connect); L/R cycle the desktop hotkey shown on
    // screen, M sends it.
    int mic_hotkey_idx_ = 0;
    lv_obj_t* mic_rec_label_ = nullptr;
    lv_obj_t* mic_key_label_ = nullptr;
    int64_t mic_rec_start_ms_ = 0;
    int mic_rec_last_sec_ = -1;
    static constexpr const char* kMicHotkeyNames[] = {"VOICE", "ENTER", "DEL"};
    static constexpr int kMicHotkeyCount = 3;

    int ticks_ = 0;
    const int kChgCtrlInterval = 5;

    void InitializeBoardPowerManager() {
        gpio_config_t gpio_init_struct = {0};
        gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
        gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;
        gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_init_struct.pin_bit_mask = (1ull << CODEC_PWR_PIN) | (1ull << SYS_POW_PIN);
        gpio_config(&gpio_init_struct);

        gpio_set_level(CODEC_PWR_PIN, 1); 
        gpio_set_level(SYS_POW_PIN, 1); 

        gpio_config_t chg_init_struct = {0};

        chg_init_struct.intr_type = GPIO_INTR_DISABLE;
        chg_init_struct.mode = GPIO_MODE_INPUT;
        chg_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
        chg_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
        chg_init_struct.pin_bit_mask = 1ull << CHRG_PIN;
        ESP_ERROR_CHECK(gpio_config(&chg_init_struct));

        chg_init_struct.mode = GPIO_MODE_OUTPUT;
        chg_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
        chg_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
        chg_init_struct.pin_bit_mask = 1ull << CHG_CTRL_PIN;
        ESP_ERROR_CHECK(gpio_config(&chg_init_struct));
        gpio_set_level(CHG_CTRL_PIN, 1);

        if (gpio_get_level(CHRG_PIN) == 0) {
            power_status_ = kDeviceTypecSupply;
        } else {
            power_status_ = kDeviceBatterySupply;
        }

        esp_timer_create_args_t wake_display_timer_args = {
            .callback = [](void *arg) {
                atk_dnesp32s3_box0* self = static_cast<atk_dnesp32s3_box0*>(arg);
                if (self->LcdStatus_ == kDevicelcdbacklightOff && Application::GetInstance().GetDeviceState() == kDeviceStateListening 
                    && self->wake_status_ == kDeviceWaitWake) {

                    if (self->power_sleep_ == kDeviceNeutralSleep) {
                        self->power_save_timer_->WakeUp();
                    }

                    self->GetBacklight()->RestoreBrightness();
                    self->wake_status_ = kDeviceAwakened;
                    self->LcdStatus_ = kDevicelcdbacklightOn;
                } else if (self->power_sleep_ == kDeviceNeutralSleep && Application::GetInstance().GetDeviceState() == kDeviceStateListening 
                         && self->LcdStatus_ != kDevicelcdbacklightOff && self->wake_status_ == kDeviceAwakened) {
                    self->power_save_timer_->WakeUp();
                    self->power_sleep_ = kDeviceNoSleep;
                } else {
                    self->ticks_ ++;
                    if (self->ticks_ % self->kChgCtrlInterval == 0) {
                        if (gpio_get_level(CHRG_PIN) == 0) {
                            self->power_status_ = kDeviceTypecSupply;
                        } else {
                            self->power_status_ = kDeviceBatterySupply;
                        }

                        if (self->power_manager_->low_voltage_ < 2877 && self->power_status_ != kDeviceTypecSupply) {
                            esp_timer_stop(self->power_manager_->timer_handle_);
                            gpio_set_level(CHG_CTRL_PIN, 0);
                            vTaskDelay(pdMS_TO_TICKS(100));
                            gpio_set_level(SYS_POW_PIN, 0);     
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                    }
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wake_update_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&wake_display_timer_args, &wake_timer_handle_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(wake_timer_handle_, 300000));
    }

    void InitializePowerManager() {
        power_manager_ = new PowerManager(CHRG_PIN);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    // Personal default WiFi credentials (from box0_local_config.h, generated
    // from box0_config.ini by flash_box0.bat) so the board connects without
    // the manual provisioning flow. Each default is added only if its SSID is
    // not saved yet; existing entries keep their order (NVS stays first).
    void EnsureDefaultWifi() {
        auto& ssid_manager = SsidManager::GetInstance();
        auto ensure = [&ssid_manager](const char* ssid, const char* password) {
            const auto& list = ssid_manager.GetSsidList();
            bool exists = std::any_of(list.begin(), list.end(),
                [&](const SsidItem& item) { return item.ssid == ssid; });
            if (!exists) {
                ssid_manager.AddSsid(ssid, password);
                ESP_LOGI(TAG, "Added default WiFi %s from box0_config.ini", ssid);
            }
        };
        ensure(BOX0_WIFI_SSID, BOX0_WIFI_PASSWORD);
#if defined(BOX0_WIFI2_SSID)
        ensure(BOX0_WIFI2_SSID, BOX0_WIFI2_PASSWORD);
#endif
#if defined(BOX0_WIFI3_SSID)
        ensure(BOX0_WIFI3_SSID, BOX0_WIFI3_PASSWORD);
#endif
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            power_sleep_ = kDeviceNeutralSleep;
            XiaozhiStatus_ = kDevice_join_Sleep;
            GetDisplay()->SetPowerSaveMode(true);

            if (LcdStatus_ != kDevicelcdbacklightOff) {
                GetBacklight()->SetBrightness(1);
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            power_sleep_ = kDeviceNoSleep;
            GetDisplay()->SetPowerSaveMode(false);

            if (XiaozhiStatus_ != kDevice_Exit_Sleep) {
                GetBacklight()->RestoreBrightness();
            }
        });
        power_save_timer_->OnShutdownRequest([this]() {
            if (power_status_ == kDeviceBatterySupply) {
                esp_timer_stop(power_manager_->timer_handle_);
                gpio_set_level(CHG_CTRL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(SYS_POW_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        });

        power_save_timer_->SetEnabled(true);
    }

    // Initialize I2C peripheral
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    // Initialize spi peripheral
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = LCD_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = LCD_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    const lv_font_t* GetMenuFont() {
        auto theme = static_cast<LvglTheme*>(display_->GetTheme());
        if (theme != nullptr) {
            menu_font_ = theme->text_font();
        }
        return menu_font_ != nullptr ? menu_font_->font() : nullptr;
    }
    void ShowBootMenu() {
        if (menu_visible_) {
            return;
        }
        DisplayLockGuard lock(display_);
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        int h = lv_display_get_vertical_resolution(disp);
        const lv_font_t* font = GetMenuFont();

        menu_layer_ = lv_obj_create(lv_layer_top());
        lv_obj_set_size(menu_layer_, w, h);
        lv_obj_set_pos(menu_layer_, 0, 0);
        lv_obj_set_style_bg_color(menu_layer_, lv_color_hex(0x101418), 0);
        lv_obj_set_style_bg_opa(menu_layer_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(menu_layer_, 0, 0);
        lv_obj_set_style_radius(menu_layer_, 0, 0);
        lv_obj_set_style_pad_all(menu_layer_, 0, 0);
        lv_obj_remove_flag(menu_layer_, LV_OBJ_FLAG_SCROLLABLE);

        // Bottom hint kept deliberately dim so it does not compete with items
        lv_obj_t* hint = lv_label_create(menu_layer_);
        lv_label_set_text(hint, "L/R: Switch   M: Enter");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x424950), 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(hint, font, 0);
        }
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

        menu_page_label_ = lv_label_create(menu_layer_);
        lv_obj_set_style_text_color(menu_page_label_, lv_color_hex(0x424950), 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(menu_page_label_, font, 0);
        }
        lv_obj_align(menu_page_label_, LV_ALIGN_TOP_RIGHT, -8, 10);

        menu_page_ = -1; // force the page (re)build below
        RenderMenuPage();
        menu_visible_ = true;
    }

    // (Re)builds the menu rows for the page containing menu_index_ and updates
    // the selection highlight plus the page indicator. Caller must hold the
    // display lock.
    void RenderMenuPage() {
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        const lv_font_t* font = GetMenuFont();
        static const char* kTexts[kMenuItemCount] = {
            "AI Voice", "Wireless Mic", "Flappy Bird", "Dino Run", "About", "Power",
        };
        int page = menu_index_ / kMenuItemsPerPage;
        int npages = (kMenuItemCount + kMenuItemsPerPage - 1) / kMenuItemsPerPage;
        if (page != menu_page_) {
            for (int i = 0; i < kMenuItemsPerPage; i++) {
                if (menu_items_[i] != nullptr) {
                    lv_obj_delete(menu_items_[i]);
                    menu_items_[i] = nullptr;
                }
            }
            menu_page_ = page;
            for (int i = 0; i < kMenuItemsPerPage; i++) {
                int idx = page * kMenuItemsPerPage + i;
                if (idx >= kMenuItemCount) {
                    break;
                }
                lv_obj_t* row = lv_obj_create(menu_layer_);
                lv_obj_set_size(row, w, 56);
                lv_obj_set_pos(row, 0, 16 + i * 64);
                lv_obj_set_style_radius(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_style_bg_color(row, lv_color_hex(0x2F6FED), 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_t* label = lv_label_create(row);
                lv_label_set_text(label, kTexts[idx]);
                lv_obj_set_style_text_color(label, lv_color_hex(0x9AA4AD), 0);
                if (font != nullptr) {
                    lv_obj_set_style_text_font(label, font, 0);
                }
                lv_obj_align(label, LV_ALIGN_LEFT_MID, 22, 0);
                menu_items_[i] = row;
            }
        }
        for (int i = 0; i < kMenuItemsPerPage; i++) {
            if (menu_items_[i] == nullptr) {
                continue;
            }
            int idx = page * kMenuItemsPerPage + i;
            lv_obj_set_style_bg_opa(menu_items_[i], (idx == menu_index_) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_t* label = lv_obj_get_child(menu_items_[i], 0);
            if (label != nullptr) {
                lv_obj_set_style_text_color(label, (idx == menu_index_) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x9AA4AD), 0);
            }
        }
        if (menu_page_label_ != nullptr) {
            char p[8];
            snprintf(p, sizeof(p), "%d/%d", (page + 1) % 100, npages % 100);
            lv_label_set_text(menu_page_label_, p);
        }
    }

    void HideBootMenu() {
        if (!menu_visible_) {
            return;
        }
        DisplayLockGuard lock(display_);
        lv_obj_delete(menu_layer_);
        menu_layer_ = nullptr;
        for (int i = 0; i < kMenuItemsPerPage; i++) {
            menu_items_[i] = nullptr;
        }
        menu_page_label_ = nullptr;
        menu_page_ = -1;
        menu_visible_ = false;
    }

    void SelectMenuItem(int index) {
        if (!menu_visible_ || index == menu_index_) {
            return;
        }
        menu_index_ = index;
        DisplayLockGuard lock(display_);
        RenderMenuPage();
    }

    // Shared L/R navigation for the about/power/menu layers. Fires on press
    // (not release) so the highlight tracks the finger. Returns true when the
    // press was consumed; the caller must then ignore the release click.
    bool NavSelection(int dir) {
        if (about_layer_ != nullptr) {
            SelectAboutPage((about_page_ + dir + kAboutPages) % kAboutPages);
            return true;
        }
        if (power_layer_ != nullptr) {
            SelectPowerItem((power_index_ + 1) % 2);
            return true;
        }
        if (menu_visible_) {
            SelectMenuItem((menu_index_ + dir + kMenuItemCount) % kMenuItemCount);
            return true;
        }
        return false;
    }

    void StopNavRepeat() {
        nav_repeat_dir_ = 0;
        if (nav_repeat_timer_ != nullptr) {
            esp_timer_stop(nav_repeat_timer_);
        }
    }

    // Hold-to-repeat scrolling, main menu only (other pages keep their
    // long-press gestures, so repeating there would fight them).
    void StartNavRepeat(int dir) {
        if (nav_repeat_timer_ == nullptr) {
            return;
        }
        StopNavRepeat();
        nav_repeat_dir_ = dir;
        nav_repeat_ticks_ = 0;
        esp_timer_start_periodic(nav_repeat_timer_, 40 * 1000);
    }

    void NavRepeatTick() {
        if (nav_repeat_dir_ == 0) {
            return;
        }
        gpio_num_t pin = nav_repeat_dir_ > 0 ? R_BUTTON_GPIO : L_BUTTON_GPIO;
        if (gpio_get_level(pin) != 0 || !menu_visible_) {
            StopNavRepeat();
            return;
        }
        nav_repeat_ticks_++;
        if (nav_repeat_ticks_ < 10) {
            return;  // ~400 ms before repeat starts
        }
        if ((nav_repeat_ticks_ - 10) % 3 != 0) {
            return;  // then ~every 120 ms
        }
        SelectMenuItem((menu_index_ + nav_repeat_dir_ + kMenuItemCount) % kMenuItemCount);
    }

    void ActivateMenuItem() {
        if (menu_index_ == 0) {
            HideBootMenu();
        } else if (menu_index_ == 1) {
            HideBootMenu();
            ShowMicPage();
        } else if (menu_index_ == 2) {
            HideBootMenu();
            StartGame();
        } else if (menu_index_ == 3) {
            HideBootMenu();
            StartDinoGame();
        } else if (menu_index_ == 4) {
            ShowAboutPage();
        } else {
            ShowPowerPage();
        }
    }

    void UpdateAboutPage() {
        // Caller must hold the display lock.
        if (about_layer_ == nullptr || about_body_label_ == nullptr || about_title_label_ == nullptr) {
            return;
        }
        char title[24];
        snprintf(title, sizeof(title), "About %d/%d", about_page_ + 1, kAboutPages);
        lv_label_set_text(about_title_label_, title);

        char lines[512];
        if (about_page_ == 0) {
            // Page 1: identity + network (the important stuff first)
            const esp_app_desc_t* desc = esp_app_get_description();
            char build_str[24];
            {
                // __DATE__ "Mmm dd yyyy" -> "yyyy-mm-dd", __TIME__ "hh:mm:ss" -> "hh:mm"
                static const char* kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
                char mmm[4] = { __DATE__[0], __DATE__[1], __DATE__[2], 0 };
                const char* m = strstr(kMonths, mmm);
                int month = (m != nullptr) ? (int)(m - kMonths) / 3 + 1 : 0;
                snprintf(build_str, sizeof(build_str), "%.4s-%02d-%02d %.5s", __DATE__ + 7, month % 100, atoi(__DATE__ + 4) % 100, __TIME__);
            }
            char ip_str[16] = "-";
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info;
            if (netif != nullptr && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            }
            char ssid_str[33] = "-";
            char rssi_str[24] = "-";
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                strncpy(ssid_str, (const char*)ap_info.ssid, sizeof(ssid_str) - 1);
                ssid_str[sizeof(ssid_str) - 1] = '\0';
                snprintf(rssi_str, sizeof(rssi_str), "%d dBm", (int)ap_info.rssi);
            }
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            snprintf(lines, sizeof(lines),
                "ATK BOX0  v%s\n"
                "%s\n"
                "IP: %s\n"
                "SSID: %s\n"
                "RSSI: %s\n"
                "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
                "Batt: %u%%%s",
                desc->version, build_str, ip_str, ssid_str, rssi_str,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                (unsigned)power_manager_->GetBatteryLevel(),
                power_manager_->IsCharging() ? " (chg)" : "");
        } else {
            // Page 2: chip and memory capacities
            esp_chip_info_t chip;
            esp_chip_info(&chip);
            unsigned ram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
            unsigned ram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
            unsigned psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / (1024 * 1024);
            unsigned psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
            int64_t up_s = esp_timer_get_time() / 1000000;
            snprintf(lines, sizeof(lines),
                "Chip: ESP32-S3 r%d.%d\n"
                "CPU: %d cores 240MHz\n"
                "RAM: %uK (free %uK)\n"
                "PSRAM: %uM (free %uK)\n"
                "Flash: 16MB\n"
                "Up: %02d:%02d:%02d",
                chip.revision / 100, chip.revision % 100,
                (int)chip.cores,
                ram_total, ram_free,
                psram_total, psram_free,
                (int)(up_s / 3600), (int)((up_s % 3600) / 60), (int)(up_s % 60));
        }
        lv_label_set_text(about_body_label_, lines);
    }

    void SelectAboutPage(int page) {
        if (about_layer_ == nullptr) {
            return;
        }
        about_page_ = page;
        DisplayLockGuard lock(display_);
        UpdateAboutPage();
    }

    void ShowAboutPage() {
        if (about_layer_ != nullptr) {
            return;
        }
        DisplayLockGuard lock(display_);
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        int h = lv_display_get_vertical_resolution(disp);
        const lv_font_t* font = GetMenuFont();

        about_layer_ = lv_obj_create(lv_layer_top());
        lv_obj_set_size(about_layer_, w, h);
        lv_obj_set_pos(about_layer_, 0, 0);
        lv_obj_set_style_bg_color(about_layer_, lv_color_hex(0x101418), 0);
        lv_obj_set_style_bg_opa(about_layer_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(about_layer_, 0, 0);
        lv_obj_set_style_radius(about_layer_, 0, 0);
        lv_obj_set_style_pad_all(about_layer_, 0, 0);
        lv_obj_remove_flag(about_layer_, LV_OBJ_FLAG_SCROLLABLE);

        about_title_label_ = lv_label_create(about_layer_);
        lv_obj_set_style_text_color(about_title_label_, lv_color_hex(0xFFFFFF), 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(about_title_label_, font, 0);
        }
        lv_obj_align(about_title_label_, LV_ALIGN_TOP_MID, 0, 12);

        about_body_label_ = lv_label_create(about_layer_);
        lv_obj_set_style_text_color(about_body_label_, lv_color_hex(0xD5DBE1), 0);
        lv_obj_set_style_text_line_space(about_body_label_, 2, 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(about_body_label_, font, 0);
        }
        lv_obj_align(about_body_label_, LV_ALIGN_TOP_MID, 0, 40);

        about_page_ = 0;
        UpdateAboutPage();
    }

    void HideAboutPage() {
        if (about_layer_ == nullptr) {
            return;
        }
        DisplayLockGuard lock(display_);
        lv_obj_delete(about_layer_);
        about_layer_ = nullptr;
        about_body_label_ = nullptr;
        about_title_label_ = nullptr;
        about_page_ = 0;
    }

    // ---------------- Power submenu ("Power Off" / "Reboot") ----------------
    // Hard power cut, same sequence as the battery shutdown paths: release
    // the charger control line, then drop the self-hold power line.
    void PowerOffBoard() {
        esp_timer_stop(power_manager_->timer_handle_);
        gpio_set_level(CHG_CTRL_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(SYS_POW_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    void ConfirmPowerItem() {
        if (power_index_ == 0) {
            // Power Off: only meaningful on battery; on USB supply the latch
            // stays fed, so ask the user to unplug instead.
            if (power_status_ == kDeviceBatterySupply) {
                if (power_hint_label_ != nullptr) {
                    DisplayLockGuard lock(display_);
                    lv_label_set_text(power_hint_label_, "Powering off...");
                }
                vTaskDelay(pdMS_TO_TICKS(300));
                PowerOffBoard();
            } else if (power_hint_label_ != nullptr) {
                DisplayLockGuard lock(display_);
                lv_label_set_text(power_hint_label_, "Please unplug USB first");
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
    }

    void ShowPowerPage() {
        if (power_layer_ != nullptr) {
            return;
        }
        DisplayLockGuard lock(display_);
        auto disp = lv_display_get_default();
        int w = lv_display_get_horizontal_resolution(disp);
        int h = lv_display_get_vertical_resolution(disp);
        const lv_font_t* font = GetMenuFont();

        power_layer_ = lv_obj_create(lv_layer_top());
        lv_obj_set_size(power_layer_, w, h);
        lv_obj_set_pos(power_layer_, 0, 0);
        lv_obj_set_style_bg_color(power_layer_, lv_color_hex(0x101418), 0);
        lv_obj_set_style_bg_opa(power_layer_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(power_layer_, 0, 0);
        lv_obj_set_style_radius(power_layer_, 0, 0);
        lv_obj_set_style_pad_all(power_layer_, 0, 0);
        lv_obj_remove_flag(power_layer_, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* title = lv_label_create(power_layer_);
        lv_label_set_text(title, "Power");
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(title, font, 0);
        }
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

        const char* texts[2] = { "Power Off", "Reboot" };
        for (int i = 0; i < 2; i++) {
            lv_obj_t* row = lv_obj_create(power_layer_);
            lv_obj_set_size(row, w, 36);
            lv_obj_set_pos(row, 0, 48 + i * 40);
            lv_obj_set_style_radius(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2F6FED), 0);
            lv_obj_set_style_bg_opa(row, (i == power_index_) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t* label = lv_label_create(row);
            lv_label_set_text(label, texts[i]);
            lv_obj_set_style_text_color(label, (i == power_index_) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x9AA4AD), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(label, font, 0);
            }
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 22, 0);
            power_items_[i] = row;
        }

        power_hint_label_ = lv_label_create(power_layer_);
        lv_label_set_text(power_hint_label_, "L/R: Switch   M: Confirm");
        lv_obj_set_style_text_color(power_hint_label_, lv_color_hex(0x8A939B), 0);
        if (font != nullptr) {
            lv_obj_set_style_text_font(power_hint_label_, font, 0);
        }
        lv_obj_align(power_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -6);
    }

    void HidePowerPage() {
        if (power_layer_ == nullptr) {
            return;
        }
        DisplayLockGuard lock(display_);
        lv_obj_delete(power_layer_);
        power_layer_ = nullptr;
        power_items_[0] = nullptr;
        power_items_[1] = nullptr;
        power_hint_label_ = nullptr;
        power_index_ = 0;
    }

    void SelectPowerItem(int index) {
        if (power_layer_ == nullptr || index == power_index_) {
            return;
        }
        power_index_ = index;
        DisplayLockGuard lock(display_);
        for (int i = 0; i < 2; i++) {
            if (power_items_[i] == nullptr) {
                continue;
            }
            lv_obj_set_style_bg_opa(power_items_[i], (i == power_index_) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_t* label = lv_obj_get_child(power_items_[i], 0);
            if (label != nullptr) {
                lv_obj_set_style_text_color(label, (i == power_index_) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x9AA4AD), 0);
            }
        }
    }
    // ---------------- Wireless mic (MicYou protocol) ----------------
    // Windows side: MicYou (https://github.com/LanRhyme/MicYou) listens on
    // TCP 9123 and announces itself via mDNS as "_micyou._tcp".
    // Wire format (tauri-app/crates/micyou-protocol/proto/network.proto):
    //   1. Connect TCP, send "MicYouCheck1", expect "MicYouCheck2".
    //   2. First frame: MessageWrapper{connect: ConnectMessage{session_id}}.
    //   3. Audio frames on the same TCP connection (TCP-only mode).
    //   Frame header (8 bytes): magic 0x4D696359 "MicY" (BE) + payload len (i32, BE).
    //   AudioPacketMessage: buffer=PCM, sample_rate=16000, channel_count=1,
    //   audio_format=2 (PCM 16-bit), codec=0 (raw PCM, proto3 default, omitted).

    static int64_t MicNowMs() {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    static size_t MicEncodeVarint(uint8_t* out, uint64_t v) {
        size_t n = 0;
        while (v >= 0x80) {
            out[n++] = (uint8_t)(v | 0x80);
            v >>= 7;
        }
        out[n++] = (uint8_t)v;
        return n;
    }

    static int MicSendAll(int sock, const uint8_t* data, size_t len) {
        size_t sent = 0;
        int retries = 0;
        while (sent < len) {
            int r = send(sock, (const char*)data + sent, len - sent, 0);
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (++retries > 100) {
                        return -1;
                    }
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
                return -1;
            }
            sent += (size_t)r;
        }
        return 0;
    }

    static int MicRecvExact(int sock, uint8_t* buf, size_t len, int timeout_ms) {
        size_t got = 0;
        int64_t deadline = esp_timer_get_time() / 1000 + timeout_ms;
        while (got < len) {
            int r = recv(sock, (char*)buf + got, len - got, 0);
            if (r > 0) {
                got += (size_t)r;
                continue;
            }
            if (r == 0) {
                return -1;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (esp_timer_get_time() / 1000 >= deadline) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        return 0;
    }

    // Wraps an inner protobuf message into a framed MessageWrapper.
    size_t MicWrapFrame(uint8_t* out, size_t cap, uint8_t wrapper_tag, const uint8_t* inner, size_t inner_len) {
        uint8_t body[16 + 768];
        if (inner_len > 768) {
            return 0;
        }
        size_t w = 0;
        body[w++] = wrapper_tag;
        w += MicEncodeVarint(body + w, inner_len);
        memcpy(body + w, inner, inner_len);
        w += inner_len;
        if (w + 8 > cap) {
            return 0;
        }
        out[0] = 0x4D; // "MicY"
        out[1] = 0x69;
        out[2] = 0x63;
        out[3] = 0x59;
        out[4] = (uint8_t)(w >> 24);
        out[5] = (uint8_t)(w >> 16);
        out[6] = (uint8_t)(w >> 8);
        out[7] = (uint8_t)w;
        memcpy(out + 8, body, w);
        return w + 8;
    }

    size_t MicBuildConnectFrame(uint8_t* out, size_t cap) {
        uint8_t conn[12];
        size_t c = 0;
        conn[c++] = 0x08; // ConnectMessage field 1: session_id
        c += MicEncodeVarint(conn + c, (uint64_t)mic_session_id_);
        return MicWrapFrame(out, cap, 0x12, conn, c); // MessageWrapper field 2: connect
    }

    size_t MicBuildTimestampFrame(uint8_t* out, size_t cap, uint8_t wrapper_tag, int64_t ts) {
        uint8_t inner[12];
        size_t c = 0;
        inner[c++] = 0x08; // Ping/PongMessage field 1: timestamp
        c += MicEncodeVarint(inner + c, (uint64_t)ts);
        return MicWrapFrame(out, cap, wrapper_tag, inner, c);
    }

    size_t MicBuildAudioFrame(uint8_t* out, size_t cap, const uint8_t* pcm, size_t pcm_len) {
        if (pcm_len > 640) {
            return 0;
        }
        // AudioPacketMessage
        uint8_t ap[16 + 640];
        size_t p = 0;
        ap[p++] = 0x0A; // field 1: buffer
        p += MicEncodeVarint(ap + p, pcm_len);
        memcpy(ap + p, pcm, pcm_len);
        p += pcm_len;
        ap[p++] = 0x10; // field 2: sample_rate
        p += MicEncodeVarint(ap + p, 16000);
        ap[p++] = 0x18; // field 3: channel_count
        p += MicEncodeVarint(ap + p, 1);
        ap[p++] = 0x20; // field 4: audio_format = 2 (PCM 16-bit)
        p += MicEncodeVarint(ap + p, 2);
        // AudioPacketMessageOrdered
        uint8_t ord[32 + sizeof(ap)];
        size_t q = 0;
        ord[q++] = 0x08; // field 1: sequence_number
        q += MicEncodeVarint(ord + q, (uint32_t)mic_seq_++);
        ord[q++] = 0x12; // field 2: audio_packet
        q += MicEncodeVarint(ord + q, p);
        memcpy(ord + q, ap, p);
        q += p;
        ord[q++] = 0x18; // field 3: timestamp
        q += MicEncodeVarint(ord + q, (uint64_t)MicNowMs());
        ord[q++] = 0x30; // field 6: session_id
        q += MicEncodeVarint(ord + q, (uint64_t)mic_session_id_);
        return MicWrapFrame(out, cap, 0x0A, ord, q); // MessageWrapper field 1: audio_packet
    }

    // Drains incoming TCP data and answers server pings. Returns false if broken.
    bool MicDrainRx(int sock) {
        uint8_t tmp[256];
        while (true) {
            int r = recv(sock, (char*)tmp, sizeof(tmp), 0);
            if (r == 0) {
                ESP_LOGW(TAG, "MicYou server closed the connection");
                return false;
            }
            if (r < 0) {
                return errno == EAGAIN || errno == EWOULDBLOCK;
            }
            if (mic_rx_len_ + (size_t)r > sizeof(mic_rx_acc_)) {
                mic_rx_len_ = 0; // overflow: drop and resync
            }
            memcpy(mic_rx_acc_ + mic_rx_len_, tmp, (size_t)r);
            mic_rx_len_ += (size_t)r;
            size_t off = 0;
            bool resync = false;
            while (!resync && mic_rx_len_ - off >= 8) {
                const uint8_t* f = mic_rx_acc_ + off;
                if (f[0] != 0x4D || f[1] != 0x69 || f[2] != 0x63 || f[3] != 0x59) {
                    resync = true;
                    break;
                }
                uint32_t plen = ((uint32_t)f[4] << 24) | ((uint32_t)f[5] << 16) | ((uint32_t)f[6] << 8) | f[7];
                if (plen > 4096) {
                    resync = true;
                    break;
                }
                if (mic_rx_len_ - off < 8 + plen) {
                    break; // incomplete frame, wait for more data
                }
                const uint8_t* p = f + 8;
                // PingMessage: wrapper field 5 (tag 0x2A), inner field 1 (tag 0x08) varint timestamp
                if (plen >= 3 && p[0] == 0x2A && p[2] == 0x08) {
                    uint64_t ts = 0;
                    int shift = 0;
                    bool ok = false;
                    for (size_t i = 3; i < plen && i < 13; i++) {
                        uint8_t b = p[i];
                        ts |= (uint64_t)(b & 0x7F) << shift;
                        shift += 7;
                        if ((b & 0x80) == 0) {
                            ok = true;
                            break;
                        }
                    }
                    if (ok) {
                        uint8_t pong[24];
                        size_t pl = MicBuildTimestampFrame(pong, sizeof(pong), 0x32, (int64_t)ts);
                        if (pl == 0 || MicSendAll(sock, pong, pl) != 0) {
                            return false;
                        }
                    }
                }
                off += 8 + plen;
            }
            if (resync) {
                mic_rx_len_ = 0;
            } else if (off > 0) {
                memmove(mic_rx_acc_, mic_rx_acc_ + off, mic_rx_len_ - off);
                mic_rx_len_ -= off;
            }
        }
    }

    void MicSetStatus(const char* status, bool streaming = false) {
        DisplayLockGuard lock(display_);
        if (mic_status_label_ != nullptr) {
            lv_label_set_text(mic_status_label_, status);
            lv_obj_set_style_text_color(mic_status_label_, streaming ? lv_color_hex(0x4ADE80) : lv_color_hex(0xD5DBE1), 0);
        }
    }

    void MicSetPc(const char* pc) {
        DisplayLockGuard lock(display_);
        if (mic_pc_label_ != nullptr) {
            lv_label_set_text(mic_pc_label_, pc);
        }
    }

    static void MicTaskEntry(void* arg) {
        auto* self = static_cast<atk_dnesp32s3_box0*>(arg);
        self->MicTaskRun();
        self->mic_task_handle_ = nullptr;
        vTaskDelete(nullptr);
    }

    void MicTaskRun() {
        // Recording is tied to the page session: every successful connection
        // starts a new recording on the PC (REC_START), every disconnect or
        // page exit ends it (REC_STOP). Reconnects automatically while open.
        while (!mic_task_stop_) {
            int sock = MicConnectSession();
            if (sock < 0) {
                if (!mic_task_stop_) {
                    MicSetStatus("Reconnecting...");
                    for (int i = 0; i < 10 && !mic_task_stop_; i++) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                }
                continue;
            }
            // Connected: stream and record continuously until the link drops
            mic_streaming_ = true;
            MicSetStatus("Streaming...", true);
            mic_rec_start_ms_ = MicNowMs();
            mic_rec_last_sec_ = -1;
            MicSignalPc("REC_START");
            MicStreamLoop(sock);
            close(sock);
            mic_streaming_ = false;
            MicSignalPc("REC_STOP");
        }
        // Restore the audio engine once, at the very end
        if (mic_engine_muted_) {
            Application::GetInstance().GetAudioService().EnableWakeWordDetection(true);
            mic_engine_muted_ = false;
        }
        ESP_LOGI(TAG, "Mic task exited");
    }

    // Discover MicYou over mDNS and perform the TCP handshake.
    // Returns the connected socket or -1 (status label already updated).
    int MicConnectSession() {
        int sock = -1;
        MicSetStatus("Searching PC...");
        esp_ip4_addr_t server_ip;
        server_ip.addr = 0;
        uint16_t server_port = 9123;
        while (!mic_task_stop_ && server_ip.addr == 0) {
            if (!mdns_started_) {
                esp_err_t err = mdns_init();
                if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
                    mdns_started_ = true;
                } else {
                    ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
                    break;
                }
            }
            mdns_result_t* results = nullptr;
            mdns_query_ptr("_micyou", "_tcp", 3000, 4, &results);
            for (mdns_result_t* r = results; r != nullptr && server_ip.addr == 0; r = r->next) {
                for (mdns_ip_addr_t* a = r->addr; a != nullptr; a = a->next) {
                    if (a->addr.type == ESP_IPADDR_TYPE_V4 && r->port != 0) {
                        server_ip.addr = a->addr.u_addr.ip4.addr;
                        server_port = r->port;
                        break;
                    }
                }
            }
            if (results != nullptr) {
                mdns_query_results_free(results);
            }
            if (server_ip.addr == 0) {
                return -1;  // outer loop retries
            }
        }
        if (server_ip.addr != 0) {
            char pc_str[48];
            snprintf(pc_str, sizeof(pc_str), "PC: " IPSTR ":%u", IP2STR(&server_ip), (unsigned)server_port);
            MicSetPc(pc_str);
            mic_pc_ip_ = server_ip;
        }

        if (!mic_task_stop_ && server_ip.addr != 0) {
            MicSetStatus("Connecting...");
            sock = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock >= 0) {
                struct sockaddr_in sa;
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_port = htons(server_port);
                sa.sin_addr.s_addr = server_ip.addr;
                fcntl(sock, F_SETFL, O_NONBLOCK);
                int rc = connect(sock, (struct sockaddr*)&sa, sizeof(sa));
                if (rc < 0 && errno == EINPROGRESS) {
                    fd_set wf;
                    FD_ZERO(&wf);
                    FD_SET(sock, &wf);
                    struct timeval tv;
                    tv.tv_sec = 3;
                    tv.tv_usec = 0;
                    rc = select(sock + 1, nullptr, &wf, nullptr, &tv);
                    if (rc > 0) {
                        int soerr = 0;
                        socklen_t elen = sizeof(soerr);
                        getsockopt(sock, SOL_SOCKET, SO_ERROR, &soerr, &elen);
                        rc = soerr == 0 ? 0 : -1;
                    } else {
                        rc = -1;
                    }
                }
                if (rc == 0) {
                    // Blocking with a short timeout for the handshake phase
                    int flags = fcntl(sock, F_GETFL, 0);
                    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
                    struct timeval tv;
                    tv.tv_sec = 0;
                    tv.tv_usec = 500000;
                    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    mic_session_id_ = (((int64_t)esp_random()) << 32 | (int64_t)esp_random()) & 0x7FFFFFFFFFFFFFFFLL;
                    uint8_t hs[12];
                    if (MicSendAll(sock, (const uint8_t*)"MicYouCheck1", 12) == 0 &&
                        MicRecvExact(sock, hs, sizeof(hs), 5000) == 0 &&
                        memcmp(hs, "MicYouCheck2", 12) == 0) {
                        size_t flen = MicBuildConnectFrame(mic_frame_buf_, sizeof(mic_frame_buf_));
                        if (flen > 0 && MicSendAll(sock, mic_frame_buf_, flen) == 0) {
                            // Non-blocking + TCP_NODELAY for the streaming loop
                            flags = fcntl(sock, F_GETFL, 0);
                            fcntl(sock, F_SETFL, flags | O_NONBLOCK);
                            int one = 1;
                            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                            mic_rx_len_ = 0;
                            mic_seq_ = 0;
                            return sock;
                        }
                    }
                }
                ESP_LOGW(TAG, "MicYou connect/handshake failed");
                MicSetStatus("Connect failed, retrying...");
                close(sock);
                sock = -1;
            } else {
                MicSetStatus("Socket error, retrying...");
            }
        }
        return -1;
    }

    // Push PCM frames while mic_streaming_ is set; returns on link failure.
    void MicStreamLoop(int sock) {
        int64_t last_ping_ms = 0;
        while (!mic_task_stop_) {
            if (mic_streaming_) {
                if (!mic_engine_muted_) {
                    // Take over the microphone from the audio input task
                    auto& audio = Application::GetInstance().GetAudioService();
                    audio.EnableWakeWordDetection(false);
                    audio.EnableVoiceProcessing(false);
                    mic_engine_muted_ = true;
                    vTaskDelay(pdMS_TO_TICKS(150));
                    continue;
                }
                std::vector<int16_t> data;
                if (Application::GetInstance().GetAudioService().ReadAudioData(data, 16000, 320)) {
                    int16_t* pcm = data.data();
                    size_t nsamples = data.size();
                    std::vector<int16_t> mono;
                    if (GetAudioCodec()->input_channels() == 2) {
                        mono.resize(nsamples / 2);
                        for (size_t i = 0, j = 0; i < mono.size(); i++, j += 2) {
                            mono[i] = data[j];
                        }
                        pcm = mono.data();
                        nsamples = mono.size();
                    }
                    if (MicNowMs() < mic_mute_until_ms_.load()) {
                        // Button-press mute window: send silence instead of the click
                        memset(pcm, 0, nsamples * sizeof(int16_t));
                    }
                    int32_t peak = 0;
                    for (size_t i = 0; i < nsamples; i++) {
                        int32_t v = pcm[i];
                        if (v < 0) { v = -v; }
                        if (v > peak) { peak = v; }
                    }
                    mic_level_.store(peak);
                    size_t flen = MicBuildAudioFrame(mic_frame_buf_, sizeof(mic_frame_buf_),
                                                     (const uint8_t*)pcm, nsamples * sizeof(int16_t));
                    if (flen == 0 || MicSendAll(sock, mic_frame_buf_, flen) != 0) {
                        ESP_LOGW(TAG, "Mic audio send failed: errno=%d", errno);
                        MicSetStatus("Send failed, retrying...");
                        break;
                    }
                    if (!MicDrainRx(sock)) {
                        ESP_LOGW(TAG, "Mic RX error while streaming");
                        MicSetStatus("Disconnected, retrying...");
                        break;
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
                if (!MicDrainRx(sock)) {
                    ESP_LOGW(TAG, "Mic RX error while idle");
                    MicSetStatus("Disconnected, retrying...");
                    break;
                }
                int64_t now = MicNowMs();
                if (now - last_ping_ms > 3000) {
                    last_ping_ms = now;
                    size_t flen = MicBuildTimestampFrame(mic_frame_buf_, sizeof(mic_frame_buf_), 0x2A, now);
                    if (flen == 0 || MicSendAll(sock, mic_frame_buf_, flen) != 0) {
                        ESP_LOGW(TAG, "Mic keepalive send failed: errno=%d", errno);
                        MicSetStatus("Disconnected, retrying...");
                        break;
                    }
                }
            }
        }
    }

    // ---------------- Key-press sound cues ----------------
    // Button clicks travel through the chassis into the microphone. While
    // streaming, zero the outgoing audio for a short window after each press
    // so the vibration is neither streamed to the PC nor drawn on the level
    // waveform. Arm the window from OnPressDown (not OnClick) because the
    // click noise happens at press-down, before click is even detected.
    void MicMuteFor(int64_t ms) {
        int64_t until = MicNowMs() + ms;
        if (mic_mute_until_ms_.load() < until) {
            mic_mute_until_ms_.store(until);
        }
        mic_level_.store(0);
    }

    // Short UI cues synthesized at runtime and written straight to the codec
    // as mono PCM at the codec output rate. A cue is a list of segments; each
    // segment is a percussive tone gliding exponentially from freq_start to
    // freq_end (equal = steady note) with a fast attack and decaying tail.
    struct MicCueSegment {
        float freq_start;
        float freq_end;
        uint16_t duration_ms;
        float gain;
    };

    // M key, start streaming: rising major-triad arpeggio C6-E6-G6 ("on")
    static constexpr MicCueSegment kMicCueStart[] = {
        {1046.5f, 1046.5f, 55, 0.65f},
        {1318.5f, 1318.5f, 55, 0.65f},
        {1568.0f, 1568.0f, 140, 0.70f},
    };
    // M key, stop streaming: falling triad G6-E6-C6 ("off")
    static constexpr MicCueSegment kMicCueStop[] = {
        {1568.0f, 1568.0f, 55, 0.60f},
        {1318.5f, 1318.5f, 55, 0.60f},
        {1046.5f, 1046.5f, 150, 0.65f},
    };
    // Right key, send text (= PC Enter): quick upward whoosh-pop
    static constexpr MicCueSegment kMicCueSend[] = {
        {900.0f, 2400.0f, 80, 0.55f},
    };
    // Long-press left, leave the page: soft downward glide
    static constexpr MicCueSegment kMicCueExit[] = {
        {1200.0f, 480.0f, 170, 0.50f},
    };

    void PlayMicCue(const MicCueSegment* segments, size_t count) {
        auto codec = GetAudioCodec();
        bool resume_output = !codec->output_enabled();
        if (resume_output) {
            codec->EnableOutput(true);
        }
        const int sample_rate = codec->output_sample_rate();
        const int chunk_samples = sample_rate / 50; // 20 ms chunks
        std::vector<int16_t> pcm(chunk_samples);

        // Brief silence to mask the PA power-up click
        memset(pcm.data(), 0, pcm.size() * sizeof(int16_t));
        codec->OutputData(pcm);

        double phase = 0.0;
        for (size_t s = 0; s < count; s++) {
            const MicCueSegment& seg = segments[s];
            int total = sample_rate * seg.duration_ms / 1000;
            int attack = std::min(sample_rate * 6 / 1000, total / 5);
            int offset = 0;
            while (offset < total) {
                int n = std::min(chunk_samples, total - offset);
                for (int i = 0; i < n; i++) {
                    float t = (float)(offset + i) / total;
                    float freq = seg.freq_start * powf(seg.freq_end / seg.freq_start, t);
                    phase += 2.0 * M_PI * freq / sample_rate;
                    int pos = offset + i;
                    float env = pos < attack
                        ? (float)pos / attack
                        : expf(-4.0f * (pos - attack) / (float)(total - attack));
                    float v = sinf((float)phase) + 0.25f * sinf((float)phase * 2) + 0.1f * sinf((float)phase * 3);
                    pcm[i] = (int16_t)(v * env * seg.gain * 20000.0f);
                }
                pcm.resize(n);
                codec->OutputData(pcm);
                offset += n;
            }
        }

        if (resume_output) {
            // Let the DMA tail drain before cutting the PA, else the cue is truncated
            int drain_ms = AUDIO_CODEC_DMA_DESC_NUM * AUDIO_CODEC_DMA_FRAME_NUM * 1000 / sample_rate + 20;
            vTaskDelay(pdMS_TO_TICKS(drain_ms));
            codec->EnableOutput(false);
        }
    }

    // Button callbacks (OnPressDown/OnClick/OnLongPress) run on the esp_timer
    // task, and it is shared by ALL buttons plus every other timer in the
    // system. Blocking it with I2S writes stalls button scanning (press
    // events get detected late -> mute window opens too late) and the timers
    // behind the LVGL tick (waveform freezes). So cues are requested here and
    // synthesized on a dedicated low-priority task instead.
    enum MicCueId { kCueStart = 0, kCueStop, kCueSend, kCueExit };
    static constexpr const MicCueSegment* kMicCueTable[] = {
        kMicCueStart, kMicCueStop, kMicCueSend, kMicCueExit,
    };
    static constexpr size_t kMicCueCount[] = {
        sizeof(kMicCueStart) / sizeof(kMicCueStart[0]),
        sizeof(kMicCueStop) / sizeof(kMicCueStop[0]),
        sizeof(kMicCueSend) / sizeof(kMicCueSend[0]),
        sizeof(kMicCueExit) / sizeof(kMicCueExit[0]),
    };

    void RequestMicCue(int cue) {
        mic_cue_request_.store(cue);
        if (mic_cue_task_handle_ == nullptr) {
            mic_cue_task_stop_ = false;
            mic_cue_task_handle_ = nullptr;
            if (xTaskCreate(MicCueTaskEntry, "box0_cue", 4096, this, 3, &mic_cue_task_handle_) != pdPASS) {
                mic_cue_task_handle_ = nullptr;
                ESP_LOGW(TAG, "cue task create failed (out of memory?)");
            }
        }
    }

    static void MicCueTaskEntry(void* arg) {
        static_cast<atk_dnesp32s3_box0*>(arg)->MicCueTask();
    }

    void MicCueTask() {
        while (true) {
            int cue = mic_cue_request_.exchange(-1);
            if (cue >= 0) {
                PlayMicCue(kMicCueTable[cue], kMicCueCount[cue]);
                continue;
            }
            if (mic_cue_task_stop_) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        mic_cue_task_handle_ = nullptr;
        vTaskDelete(nullptr);
    }

    void MicUpdateKeyLabel() {
        DisplayLockGuard lock(display_);
        if (mic_key_label_ != nullptr) {
            char buf[24];
            snprintf(buf, sizeof(buf), "KEY: %s", kMicHotkeyNames[mic_hotkey_idx_]);
            lv_label_set_text(mic_key_label_, buf);
        }
    }

    // L/R in the mic page: cycle the desktop hotkey (shown on screen);
    // M sends it (see the button handlers). dir: -1 = prev, +1 = next.
    void MicCycleHotkey(int dir) {
        mic_voice_active_ = false;  // manual switch takes over the VOICE flow
        mic_hotkey_idx_ = (mic_hotkey_idx_ + dir + kMicHotkeyCount) % kMicHotkeyCount;
        MicUpdateKeyLabel();
    }

    // VOICE two-press flow: first press starts voice input (stay on VOICE so
    // the next press stops it and drops the text into the chat box), second
    // press stops it — only then pre-select Enter for the final send.
    bool mic_voice_active_ = false;

    void MicSendHotkey() {
        char msg[24];
        int idx = mic_hotkey_idx_;
        snprintf(msg, sizeof(msg), "KEY_SEND %d", idx);
        RequestMicCue(kCueSend);
        MicSignalPc(msg);
        if (idx == 0) {
            if (mic_voice_active_) {
                mic_voice_active_ = false;
                mic_hotkey_idx_ = 1;
                MicUpdateKeyLabel();
            } else {
                mic_voice_active_ = true;
            }
        } else {
            mic_voice_active_ = false;
        }
    }

    // M long-press toggles recording: hold M ~0.8 s to pause, again to resume.
    // The audio stream itself stays connected while paused (idle keepalive).
    static void MicLongPressCb(void* arg) {
        static_cast<atk_dnesp32s3_box0*>(arg)->MicToggleRecording();
    }

    void ArmMicHoldTimer() {
        if (mic_hold_timer_ == nullptr) {
            esp_timer_create_args_t args = {};
            args.callback = &MicLongPressCb;
            args.arg = this;
            args.dispatch_method = ESP_TIMER_TASK;
            args.name = "mic_hold";
            if (esp_timer_create(&args, &mic_hold_timer_) != ESP_OK) {
                return;
            }
        }
        esp_timer_stop(mic_hold_timer_);
        esp_timer_start_once(mic_hold_timer_, 800 * 1000);
    }

    void MicToggleRecording() {
        if (mic_hold_) {
            return;
        }
        mic_hold_ = true;  // long-press consumed; suppress the click on release
        if (mic_streaming_) {
            mic_streaming_ = false;
            MicSetStatus("Recording paused");
            MicSignalPc("REC_STOP");
            RequestMicCue(kCueStop);
        } else {
            mic_streaming_ = true;
            mic_rec_start_ms_ = MicNowMs();
            mic_rec_last_sec_ = -1;
            MicSetStatus("Streaming...", true);
            MicSignalPc("REC_START");
            // Cover the cue with the mute window so it is not recorded
            MicMuteFor(500);
            RequestMicCue(kCueStart);
        }
    }

    // Notify the PC-side voice_link plugin over UDP: desktop hotkeys
    // (VOICE_HOLD_*, KEY_SEND n), recording control (REC_START/STOP) and the
    // legacy VOICE_DELETE message all go through this channel.
    void MicSignalPc(const char* msg) {
        if (mic_pc_ip_.addr == 0) {
            return;
        }
        int s = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s < 0) {
            return;
        }
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(kMicVoicePort);
        sa.sin_addr.s_addr = mic_pc_ip_.addr;
        sendto(s, msg, (int)strlen(msg), 0, (struct sockaddr*)&sa, sizeof(sa));
        close(s);
    }

    static void MicWaveTickCb(lv_timer_t* timer) {
        auto* self = static_cast<atk_dnesp32s3_box0*>(lv_timer_get_user_data(timer));
        self->MicWaveTick();
    }

    void MicWaveTick() {
        auto disp = lv_display_get_default();
        int h = lv_display_get_vertical_resolution(disp);
        int center_y = h - 36;
        const int kMaxBar = 28;  // half-height; bars extend both ways from the center line
        int32_t level = mic_level_.exchange(0);
        int v = 0;
        if (mic_streaming_) {
            v = (int)(level * kMaxBar / 32767);
            int decayed = mic_wave_hist_[19] * 3 / 4;  // smooth falloff
            if (v < decayed) { v = decayed; }
        }
        memmove(mic_wave_hist_, mic_wave_hist_ + 1, sizeof(int) * 19);
        mic_wave_hist_[19] = v;
        for (int i = 0; i < 20; i++) {
            if (mic_bars_[i] == nullptr) {
                continue;
            }
            int bh = mic_wave_hist_[i] * 2 + 2;
            lv_obj_set_size(mic_bars_[i], 7, bh);
            lv_obj_set_pos(mic_bars_[i], 12 + i * 11, center_y - bh / 2);
        }
        // Recording clock (only rewrite the label once per second)
        if (mic_rec_label_ != nullptr) {
            int sec = -1;
            if (mic_streaming_ && mic_rec_start_ms_ > 0) {
                sec = (int)((MicNowMs() - mic_rec_start_ms_) / 1000);
            }
            if (sec != mic_rec_last_sec_) {
                mic_rec_last_sec_ = sec;
                // runs on the LVGL task (like the bar updates above), no lock
                if (mic_rec_label_ != nullptr) {
                    if (sec >= 0) {
                        char buf[24];
                        snprintf(buf, sizeof(buf), "REC %02d:%02d", sec / 60, sec % 60);
                        lv_label_set_text(mic_rec_label_, buf);
                        lv_obj_set_style_text_color(mic_rec_label_, lv_color_hex(0xEF4444), 0);
                    } else {
                        lv_label_set_text(mic_rec_label_, "REC --:--");
                        lv_obj_set_style_text_color(mic_rec_label_, lv_color_hex(0x6B7280), 0);
                    }
                }
            }
        }
    }

    void ShowMicPage() {
        if (mic_layer_ != nullptr) {
            return;
        }
        {
            DisplayLockGuard lock(display_);
            auto disp = lv_display_get_default();
            int w = lv_display_get_horizontal_resolution(disp);
            int h = lv_display_get_vertical_resolution(disp);
            const lv_font_t* font = GetMenuFont();

            mic_layer_ = lv_obj_create(lv_layer_top());
            lv_obj_set_size(mic_layer_, w, h);
            lv_obj_set_pos(mic_layer_, 0, 0);
            lv_obj_set_style_bg_color(mic_layer_, lv_color_hex(0x101418), 0);
            lv_obj_set_style_bg_opa(mic_layer_, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(mic_layer_, 0, 0);
            lv_obj_set_style_radius(mic_layer_, 0, 0);
            lv_obj_set_style_pad_all(mic_layer_, 0, 0);
            lv_obj_remove_flag(mic_layer_, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* title = lv_label_create(mic_layer_);
            lv_label_set_text(title, "Wireless Mic");
            lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(title, font, 0);
            }
            lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

            char ip_str[48] = "IP: -";
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info;
            if (netif != nullptr && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                snprintf(ip_str, sizeof(ip_str), "IP: " IPSTR, IP2STR(&ip_info.ip));
            }
            lv_obj_t* ip_label = lv_label_create(mic_layer_);
            lv_label_set_text(ip_label, ip_str);
            lv_obj_set_style_text_color(ip_label, lv_color_hex(0xD5DBE1), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(ip_label, font, 0);
            }
            lv_obj_align(ip_label, LV_ALIGN_TOP_MID, 0, 44);

            mic_pc_label_ = lv_label_create(mic_layer_);
            lv_label_set_text(mic_pc_label_, "PC: -");
            lv_obj_set_style_text_color(mic_pc_label_, lv_color_hex(0xD5DBE1), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(mic_pc_label_, font, 0);
            }
            lv_obj_align(mic_pc_label_, LV_ALIGN_TOP_MID, 0, 72);

            mic_status_label_ = lv_label_create(mic_layer_);
            lv_label_set_text(mic_status_label_, "Searching PC...");
            lv_obj_set_style_text_color(mic_status_label_, lv_color_hex(0xD5DBE1), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(mic_status_label_, font, 0);
            }
            lv_obj_align(mic_status_label_, LV_ALIGN_TOP_MID, 0, 100);

            // Recording clock (red while recording, gray when idle)
            mic_rec_label_ = lv_label_create(mic_layer_);
            lv_label_set_text(mic_rec_label_, "REC --:--");
            lv_obj_set_style_text_color(mic_rec_label_, lv_color_hex(0x6B7280), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(mic_rec_label_, font, 0);
            }
            lv_obj_align(mic_rec_label_, LV_ALIGN_TOP_MID, 0, 128);

            // Currently selected desktop hotkey (L/R cycles it, M sends it)
            mic_key_label_ = lv_label_create(mic_layer_);
            char key_buf[24];
            snprintf(key_buf, sizeof(key_buf), "KEY: %s", kMicHotkeyNames[mic_hotkey_idx_]);
            lv_label_set_text(mic_key_label_, key_buf);
            lv_obj_set_style_text_color(mic_key_label_, lv_color_hex(0x4ADE80), 0);
            if (font != nullptr) {
                lv_obj_set_style_text_font(mic_key_label_, font, 0);
            }
            lv_obj_align(mic_key_label_, LV_ALIGN_TOP_MID, 0, 156);

            // Live audio level waveform (updated by MicWaveTick while streaming)
            for (int i = 0; i < 20; i++) {
                mic_bars_[i] = lv_obj_create(mic_layer_);
                lv_obj_set_size(mic_bars_[i], 7, 2);
                lv_obj_set_pos(mic_bars_[i], 12 + i * 11, h - 37);
                lv_obj_set_style_bg_color(mic_bars_[i], lv_color_hex(0x3B9EFF), 0);
                lv_obj_set_style_bg_opa(mic_bars_[i], LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(mic_bars_[i], 0, 0);
                lv_obj_set_style_radius(mic_bars_[i], 2, 0);
                lv_obj_set_style_pad_all(mic_bars_[i], 0, 0);
                lv_obj_remove_flag(mic_bars_[i], LV_OBJ_FLAG_SCROLLABLE);
            }
        }
        mic_level_.store(0);
        memset(mic_wave_hist_, 0, sizeof(mic_wave_hist_));
        mic_wave_timer_ = lv_timer_create(MicWaveTickCb, 40, this);
        // Keep the screen on and prevent sleep while this page is open
        power_save_timer_->SetEnabled(false);
        // Auto-connect to the MicYou desktop
        mic_task_stop_ = false;
        mic_streaming_ = false;
        if (mic_task_handle_ == nullptr) {
            mic_rx_len_ = 0;
            xTaskCreate(MicTaskEntry, "box0_mic", 8192, this, 5, &mic_task_handle_);
        }
    }

    void ExitMicPage() {
        if (mic_layer_ == nullptr) {
            return;
        }
        mic_task_stop_ = true;
        if (mic_hold_timer_ != nullptr) {
            esp_timer_stop(mic_hold_timer_);
        }
        if (mic_hold_) {
            mic_hold_ = false;
        }
        // REC_STOP is sent by the mic task itself when its loop exits.
        mic_streaming_ = false;
        mic_rec_start_ms_ = 0;
        mic_level_.store(0);
        // The mic task polls the stop flag every 20-100 ms; the discovery or
        // connect phases may need up to ~3 s to notice it.
        for (int i = 0; i < 80 && mic_task_handle_ != nullptr; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        // Stop the cue task; it finishes any pending cue (e.g. the exit sound)
        // before honoring the flag.
        mic_cue_task_stop_ = true;
        for (int i = 0; i < 80 && mic_cue_task_handle_ != nullptr; i++) {
            vTaskDelay(pdMS_TO_TICKS(25));
        }
        if (mic_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Mic task did not stop in time, leaving it to exit");
        }
        {
            DisplayLockGuard lock(display_);
            if (mic_wave_timer_ != nullptr) {
                lv_timer_delete(mic_wave_timer_);
                mic_wave_timer_ = nullptr;
            }
            if (mic_layer_ != nullptr) {
                lv_obj_delete(mic_layer_);
                mic_layer_ = nullptr;
                mic_status_label_ = nullptr;
                mic_pc_label_ = nullptr;
                mic_rec_label_ = nullptr;
                mic_key_label_ = nullptr;
                for (int i = 0; i < 20; i++) {
                    mic_bars_[i] = nullptr;
                }
            }
        }
        power_save_timer_->SetEnabled(true);
        ShowBootMenu();
    }

    // ---------------- Games (implementation in box0_games.h) ----------------
    void InitializeGames() {
        game_platform_.random = []() { return esp_random(); };
        game_platform_.get_font = [this]() { return GetMenuFont(); };
        game_platform_.lock = [this]() { game_lock_ = new DisplayLockGuard(display_); };
        game_platform_.unlock = [this]() { delete game_lock_; game_lock_ = nullptr; };
        flappy_game_.SetPlatform(game_platform_);
        dino_game_.SetPlatform(game_platform_);
    }

    void StartGame() {
        if (flappy_game_.IsActive() || dino_game_.IsActive()) {
            return;
        }
        flappy_game_.Start();
        // Keep the screen on while playing; also wakes the display if it was dimmed
        power_save_timer_->SetEnabled(false);
    }

    void StopGame() {
        if (!flappy_game_.IsActive()) {
            return;
        }
        flappy_game_.Stop();
        power_save_timer_->SetEnabled(true);
    }

    void StartDinoGame() {
        if (dino_game_.IsActive() || flappy_game_.IsActive()) {
            return;
        }
        dino_game_.Start();
        // Keep the screen on while playing; also wakes the display if it was dimmed
        power_save_timer_->SetEnabled(false);
    }

    void StopDinoGame() {
        if (!dino_game_.IsActive()) {
            return;
        }
        dino_game_.Stop();
        power_save_timer_->SetEnabled(true);
    }

    void InitializeButtons() {
        // Menu/page interactions fire on press-down (not on release) so the UI
        // tracks the finger; *_press_consumed_ makes the matching release
        // click a no-op. Mic-page press-down only mutes the chassis click.
        middle_button_.OnPressDown([this]() {
            if (mic_layer_ != nullptr) {
                MicMuteFor(250);
                ArmMicHoldTimer();
            }
            m_press_consumed_ = false;
            if (flappy_game_.IsActive() || dino_game_.IsActive()) {
                return;
            }
            // First press just wakes the dimmed screen
            if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
                power_save_timer_->WakeUp();
                power_sleep_ = kDeviceNoSleep;
                m_press_consumed_ = true;
                return;
            }
            if (power_layer_ != nullptr) {
                StopNavRepeat();
                ConfirmPowerItem();
                m_press_consumed_ = true;
                return;
            }
            if (menu_visible_) {
                StopNavRepeat();
                ActivateMenuItem();
                m_press_consumed_ = true;
            }
        });
        right_button_.OnPressDown([this]() {
            if (mic_layer_ != nullptr) { MicMuteFor(250); }
            nav_press_consumed_ = false;
            if (flappy_game_.IsActive() || dino_game_.IsActive()) {
                return;
            }
            // First press just wakes the dimmed screen
            if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
                power_save_timer_->WakeUp();
                power_sleep_ = kDeviceNoSleep;
                nav_press_consumed_ = true;
                return;
            }
            if (NavSelection(1)) {
                nav_press_consumed_ = true;
                if (menu_visible_) {
                    StartNavRepeat(1);
                }
            }
        });
        left_button_.OnPressDown([this]() {
            if (mic_layer_ != nullptr) { MicMuteFor(250); }
            nav_press_consumed_ = false;
            if (flappy_game_.IsActive() || dino_game_.IsActive()) {
                return;
            }
            // First press just wakes the dimmed screen
            if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
                power_save_timer_->WakeUp();
                power_sleep_ = kDeviceNoSleep;
                nav_press_consumed_ = true;
                return;
            }
            if (NavSelection(-1)) {
                nav_press_consumed_ = true;
                if (menu_visible_) {
                    StartNavRepeat(-1);
                }
            }
        });

        middle_button_.OnClick([this]() {
        if (m_press_consumed_) {
            // Already handled on press-down; swallow the release.
            m_press_consumed_ = false;
            return;
        }
        // First press just wakes the dimmed screen
        if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
            power_save_timer_->WakeUp();
            power_sleep_ = kDeviceNoSleep;
            return;
        }
        if (flappy_game_.IsActive()) {
            flappy_game_.Flap();
            return;
        }
        if (dino_game_.IsActive()) {
            dino_game_.Jump();
            return;
        }
        if (mic_layer_ != nullptr) {
            if (mic_hold_consumed_) {
                mic_hold_consumed_ = false;
                return;
            }
            // M sends the desktop hotkey selected with L/R
            MicSendHotkey();
            return;
        }
        if (power_layer_ != nullptr) {
            ConfirmPowerItem();
            return;
        }
        if (menu_visible_) {
            ActivateMenuItem();
            return;
        }
        if (flappy_game_.IsActive() || dino_game_.IsActive()) {
            return;
        }
        if (about_layer_ != nullptr) {
            return;
        }
            auto& app = Application::GetInstance();

            if (LcdStatus_ != kDevicelcdbacklightOff) {
                if (power_sleep_ == kDeviceNeutralSleep) {
                    power_save_timer_->WakeUp();
                    power_sleep_ = kDeviceNoSleep;
                }

                app.ToggleChatState();
            }
        });

        middle_button_.OnPressUp([this]() {
            if (mic_hold_timer_ != nullptr) {
                esp_timer_stop(mic_hold_timer_);
            }
            if (mic_hold_) {
                mic_hold_ = false;
                mic_hold_consumed_ = true;  // suppress the click event on release
                return;
            }
            if (LcdStatus_ == kDevicelcdbacklightOff) {
                Application::GetInstance().StopListening();
                Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                wake_status_ = kDeviceWaitWake;
            }

            if (XiaozhiStatus_ == kDevice_Distributionnetwork || XiaozhiStatus_ == kDevice_Exit_Sleep) {
                esp_timer_stop(power_manager_->timer_handle_);
                gpio_set_level(CHG_CTRL_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(SYS_POW_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            } else if (XiaozhiStatus_ == kDevice_join_Sleep) {
                GetBacklight()->RestoreBrightness();
                XiaozhiStatus_ = kDevice_null;
            }
        });

        middle_button_.OnLongPress([this]() {
            if (m_press_consumed_) {
                // Menu/power item already activated on press-down.
                return;
            }
            if (mic_layer_ != nullptr) {
                // recording toggle is handled by the 800 ms press-down timer
                return;
            }
            if (power_layer_ != nullptr) {
                return;
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }

            if (app.GetDeviceState() != kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                if (app.GetDeviceState() == kDeviceStateWifiConfiguring && power_status_ != kDeviceTypecSupply) {
                    GetBacklight()->SetBrightness(0);
                    XiaozhiStatus_ = kDevice_Distributionnetwork;
                } else if (power_status_ == kDeviceBatterySupply && LcdStatus_ != kDevicelcdbacklightOff) {
                    Application::GetInstance().StartListening();
                    GetBacklight()->SetBrightness(0);   
                    XiaozhiStatus_ = kDevice_Exit_Sleep;
                } else if (power_status_ == kDeviceTypecSupply && LcdStatus_ == kDevicelcdbacklightOn && Application::GetInstance().GetDeviceState() != kDeviceStateStarting) {
                    Application::GetInstance().StartListening();
                    GetBacklight()->SetBrightness(0);
                    LcdStatus_ = kDevicelcdbacklightOff;
                } else if (LcdStatus_ == kDevicelcdbacklightOff && (power_status_ == kDeviceTypecSupply || power_status_ == kDeviceBatterySupply)) {
                    GetDisplay()->SetChatMessage("system", "");
                    GetBacklight()->RestoreBrightness();
                    wake_status_ = kDeviceAwakened;
                    LcdStatus_ = kDevicelcdbacklightOn;
                }
            }
        });

        left_button_.OnClick([this]() {
        if (nav_press_consumed_) {
            // Navigation already happened on press-down; swallow the release.
            nav_press_consumed_ = false;
            return;
        }
        // First press just wakes the dimmed screen
        if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
            power_save_timer_->WakeUp();
            power_sleep_ = kDeviceNoSleep;
            return;
        }
        if (flappy_game_.IsActive() || dino_game_.IsActive()) {
            return;
        }
        if (about_layer_ != nullptr) {
            SelectAboutPage((about_page_ + kAboutPages - 1) % kAboutPages);
            return;
        }
        if (power_layer_ != nullptr) {
            SelectPowerItem((power_index_ + 1) % 2);
            return;
        }
        if (mic_layer_ != nullptr) {
            // Previous desktop hotkey (M sends the selected one)
            MicCycleHotkey(-1);
            return;
        }
        if (menu_visible_) {
            SelectMenuItem((menu_index_ + kMenuItemCount - 1) % kMenuItemCount);
            return;
        }
            if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
                power_save_timer_->WakeUp();
                power_sleep_ = kDeviceNoSleep;
            }

            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        left_button_.OnLongPress([this]() {
            if (mic_layer_ != nullptr) {
                RequestMicCue(kCueExit);
                ExitMicPage();
                return;
            }
            if (flappy_game_.IsActive()) {
                StopGame();
                ShowBootMenu();
                return;
            }
            if (dino_game_.IsActive()) {
                StopDinoGame();
                ShowBootMenu();
                return;
            }
            if (about_layer_ != nullptr) {
                HideAboutPage();
                return;
            }
            if (power_layer_ != nullptr) {
                HidePowerPage();
                return;
            }
            if (menu_visible_) {
                return;
            }
            ShowBootMenu();
        });

        right_button_.OnClick([this]() {
        if (nav_press_consumed_) {
            // Navigation already happened on press-down; swallow the release.
            nav_press_consumed_ = false;
            return;
        }
        // First press just wakes the dimmed screen
        if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
            power_save_timer_->WakeUp();
            power_sleep_ = kDeviceNoSleep;
            return;
        }
        if (flappy_game_.IsActive() || dino_game_.IsActive()) {
            return;
        }
        if (about_layer_ != nullptr) {
            SelectAboutPage((about_page_ + 1) % kAboutPages);
            return;
        }
        if (power_layer_ != nullptr) {
            SelectPowerItem((power_index_ + 1) % 2);
            return;
        }
        if (mic_layer_ != nullptr) {
            // Next desktop hotkey (M sends the selected one)
            MicCycleHotkey(1);
            return;
        }
        if (menu_visible_) {
            SelectMenuItem((menu_index_ + 1) % kMenuItemCount);
            return;
        }
            if (power_sleep_ == kDeviceNeutralSleep && LcdStatus_ != kDevicelcdbacklightOff) {
                power_save_timer_->WakeUp();
                power_sleep_ = kDeviceNoSleep;
            }
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        right_button_.OnLongPress([this]() {
            // Holding R scrolls the menu/pages now; don't max the volume there.
            if (menu_visible_ || about_layer_ != nullptr || power_layer_ != nullptr) {
                return;
            }
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

    }

    void InitializeSt7789Display() {
        ESP_LOGI(TAG, "Install panel IO");

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_CS_PIN;
        io_config.dc_gpio_num = LCD_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 7;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io);

        ESP_LOGI(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = LCD_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel);
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY); 
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    atk_dnesp32s3_box0() :
        right_button_(R_BUTTON_GPIO, false),
        left_button_(L_BUTTON_GPIO, false),
        middle_button_(M_BUTTON_GPIO, true) {
        EnsureDefaultWifi();
        InitializeBoardPowerManager();
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        InitializeGames();
        GetBacklight()->RestoreBrightness();
        // Show the boot menu after the device reaches idle state (display theme/fonts are ready by then)
        esp_timer_create_args_t menu_timer_args = {
            .callback = [](void* arg) {
                auto* self = static_cast<atk_dnesp32s3_box0*>(arg);
                if (self->menu_pending_ && Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
                    self->menu_pending_ = false;
                    esp_timer_stop(self->menu_timer_);
                    self->ShowBootMenu();
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "menu_show_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&menu_timer_args, &menu_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(menu_timer_, 100 * 1000));

        esp_timer_create_args_t nav_timer_args = {
            .callback = [](void* arg) {
                static_cast<atk_dnesp32s3_box0*>(arg)->NavRepeatTick();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "nav_repeat",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&nav_timer_args, &nav_repeat_timer_));
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            i2c_bus_, 
            I2C_NUM_0, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            GPIO_NUM_NC, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC, 
            AUDIO_CODEC_ES8311_ADDR, 
            false);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(atk_dnesp32s3_box0);
