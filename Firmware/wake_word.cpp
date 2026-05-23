// wake_word.cpp — Porcupine wake word + energy-based fallback for early testing
//
// HOW TO ENABLE PORCUPINE:
//   1. Go to console.picovoice.ai, get an Access Key (free for personal use)
//   2. Train "Hey Bunny" in their Console (Porcupine -> create custom wake word)
//   3. Download .ppn file for "ESP32 / ESP32-S3"
//   4. Convert with:  xxd -i hey_bunny_esp32.ppn > hey_bunny_ppn.h
//   5. Drop hey_bunny_ppn.h into /lib/porcupine/ alongside the Porcupine SDK
//   6. Get the Porcupine ESP32 C SDK (libpv_porcupine.a + pv_porcupine.h) from
//      https://github.com/Picovoice/porcupine/tree/master/lib/esp32-s3
//   7. Define USE_PORCUPINE=1 below, set PICOVOICE_ACCESS_KEY in config.h
//
// Until then, this module uses a simple energy-spike detector: shout/clap and
// it triggers. Good enough to verify the rest of the pipeline.

#include "wake_word.h"
#include "config.h"

// flip this when you've installed the Porcupine SDK
#define USE_PORCUPINE 0

#if USE_PORCUPINE
extern "C" {
#include "pv_porcupine.h"
}
// hey_bunny_ppn.h must define: unsigned char hey_bunny_esp32_ppn[];
//                              unsigned int  hey_bunny_esp32_ppn_len;
#include "hey_bunny_ppn.h"

static pv_porcupine_t* g_porcupine = nullptr;

bool wake_word_begin() {
    const float sensitivity = 0.7f;
    pv_status_t status = pv_porcupine_init(
        PICOVOICE_ACCESS_KEY,
        1,                                  // num keywords
        (const void**)&hey_bunny_esp32_ppn, // keyword model bytes
        (const int32_t*)&hey_bunny_esp32_ppn_len,
        &sensitivity,
        &g_porcupine);
    if (status != PV_STATUS_SUCCESS) {
        Serial.printf("[wake] Porcupine init failed: %d\n", (int)status);
        return false;
    }
    Serial.println("[wake] Porcupine ready ('Hey Bunny')");
    return true;
}

bool wake_word_process(const int16_t* frame) {
    int32_t keyword_index = -1;
    pv_status_t status = pv_porcupine_process(g_porcupine, frame, &keyword_index);
    if (status != PV_STATUS_SUCCESS) return false;
    return keyword_index >= 0;
}

#else  // ===== fallback: energy spike detector =====

static uint32_t g_baseline = 1000;
static uint8_t  g_loud_frames = 0;

bool wake_word_begin() {
    Serial.println("[wake] FALLBACK mode: shout/clap to wake (install Porcupine for real wake word)");
    return true;
}

bool wake_word_process(const int16_t* frame) {
    // running energy estimate
    uint64_t e = 0;
    for (int i = 0; i < WAKE_WORD_FRAME_LEN; i++) {
        int32_t s = frame[i];
        e += (uint64_t)(s * s);
    }
    e /= WAKE_WORD_FRAME_LEN;

    // slowly adapt baseline noise floor
    g_baseline = (g_baseline * 31 + (uint32_t)e) / 32;

    // is this frame much louder than baseline?
    if (e > g_baseline * 6 && e > 200000) {
        g_loud_frames++;
        if (g_loud_frames >= 3) {
            g_loud_frames = 0;
            return true;
        }
    } else {
        if (g_loud_frames > 0) g_loud_frames--;
    }
    return false;
}

#endif
