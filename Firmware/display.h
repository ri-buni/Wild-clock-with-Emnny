// display.h — OLED rendering (clock face, bunny, status)
#pragma once
#include <Arduino.h>

enum BunnyState {
    BUNNY_ON,           // awake, neutral animation
    BUNNY_LISTENING,    // hearing user, special frame
    BUNNY_THINKING,     // dots while waiting for AI
    BUNNY_SPEAKING,     // mouth-moving frame while TTS plays
    BUNNY_OFF           // sleeping animation (toggled off from phone)
};

void display_begin();
void display_set_state(BunnyState s);
void display_set_status_text(const char* txt);  // small status under bunny, nullable
void display_tick();                            // call from loop() ~30Hz
