/* XGB35 (ATM7051H + ATC2603C + rotated 320x480 ST7796S RGB565) backend for Open MainUI. */
#ifndef MAINUI_XGB35_HW_H
#define MAINUI_XGB35_HW_H

#include <SDL.h>
#include <stdbool.h>

void xgb35_pre_sdl_init(bool real_device);
void xgb35_post_sdl_init(bool real_device);
void xgb35_hw_quit(void);
void xgb35_fb_present(const SDL_Surface *screen);
int  xgb35_read_battery_percent(void);
bool xgb35_read_charging(void);
/* Battery voltage in mV from the ATC2603C OCV gauge, or -1. */
int  xgb35_read_battery_mv(void);
/* Device dashboard rows; unavailable fields are "Unknown". */
typedef struct {
    char battery[48], voltage[32], current[32], health[32], temperature[48], charger[48], uptime[48],
        kernel[64];
} XGB35Dashboard;
void xgb35_read_dashboard(XGB35Dashboard *out);
bool xgb35_set_brightness_step(int step_0_to_10);
bool xgb35_power_off(void);

#endif
