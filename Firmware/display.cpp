// display.cpp — 128x64 OLED rendering
// Layout: HH:MM centered in screen; 27x27 bunny anchored bottom-right;
// date small in bottom-left; status text replaces date when active.
#include "display.h"
#include "config.h"
#include "bitmaps.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>

// SSD1309 128x64 I2C — most 2.42" displays. If yours is SH1106, swap the line.
static U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static BunnyState g_state = BUNNY_ON;
static char g_status[24] = {0};
static uint32_t g_last_frame_swap = 0;
static uint8_t g_frame_idx = 0;

static const uint8_t* current_bitmap() {
    switch (g_state) {
        case BUNNY_OFF:
            return g_frame_idx ? BUNNY_SLEEP_B : BUNNY_SLEEP_A;
        case BUNNY_LISTENING:
            return BUNNY_NEUTRAL_A;
        case BUNNY_THINKING:
        case BUNNY_SPEAKING:
            return g_frame_idx ? BUNNY_NEUTRAL_A : BUNNY_NEUTRAL_B;
        case BUNNY_ON:
        default:
            return g_frame_idx ? BUNNY_NEUTRAL_B : BUNNY_NEUTRAL_A;
    }
}

static uint32_t frame_swap_interval() {
    switch (g_state) {
        case BUNNY_OFF:       return ANIM_FRAME_MS_OFF;
        case BUNNY_THINKING:  return 150;
        case BUNNY_SPEAKING:  return 120;
        case BUNNY_LISTENING: return 0xFFFFFFFF;
        default:              return ANIM_FRAME_MS_ON;
    }
}

void display_begin() {
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    u8g2.setBusClock(400000);
    u8g2.begin();
    u8g2.setContrast(255);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_logisoso18_tr);
    u8g2.drawStr(20, 40, "BUNBUN");
    u8g2.sendBuffer();
}

void display_set_state(BunnyState s) {
    if (s != g_state) {
        g_state = s;
        g_last_frame_swap = millis();
        g_frame_idx = 0;
    }
}

void display_set_status_text(const char* txt) {
    if (!txt) { g_status[0] = 0; return; }
    strncpy(g_status, txt, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = 0;
}

void display_tick() {
    if (millis() - g_last_frame_swap > frame_swap_interval()) {
        g_frame_idx ^= 1;
        g_last_frame_swap = millis();
    }

    u8g2.clearBuffer();

    // ----- bunny: bottom-right corner, 2px margin -----
    const int bx = 128 - BUNNY_W - 2;   // x=99
    const int by = 64 - BUNNY_H - 2;    // y=35
    u8g2.drawXBMP(bx, by, BUNNY_W, BUNNY_H, current_bitmap());

    // ----- HH:MM, centered in screen using 18px font -----
    // 18px-tall font fits comfortably to the left of the bunny.
    // u8g2_font_logisoso18_tr: digits ~13px wide, colon narrower
    time_t now = time(nullptr);
    struct tm tmnow;
    localtime_r(&now, &tmnow);
    char timebuf[8];
    snprintf(timebuf, sizeof(timebuf), "%02d:%02d", tmnow.tm_hour, tmnow.tm_min);

    u8g2.setFont(u8g2_font_logisoso18_tr);
    int tw = u8g2.getStrWidth(timebuf);
    // Center horizontally in the *full* screen, vertical center too
    int tx = (128 - tw) / 2;
    int ty = 38;   // baseline ~y=38 puts the digits roughly y=20..38
    u8g2.drawStr(tx, ty, timebuf);

    // ----- bottom-left: status or date -----
    u8g2.setFont(u8g2_font_5x8_tr);
    if (g_status[0]) {
        u8g2.drawStr(2, 62, g_status);
    } else {
        char datebuf[12];
        const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        snprintf(datebuf, sizeof(datebuf), "%s %02d/%02d",
                 days[tmnow.tm_wday], tmnow.tm_mon + 1, tmnow.tm_mday);
        u8g2.drawStr(2, 62, datebuf);
        if (g_state == BUNNY_OFF) {
            // small "zzz" near bunny
            u8g2.drawStr(bx - 14, by + 6, "z");
            u8g2.drawStr(bx - 10, by + 2, "z");
        }
    }

    u8g2.sendBuffer();
}
