// audio.cpp — I2S microphone (INMP441) + speaker (MAX98357A)
// uses legacy driver/i2s.h API (stable across arduino-esp32 v2.x)
#include "audio.h"
#include "config.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>

#define I2S_MIC_PORT   I2S_NUM_0
#define I2S_SPK_PORT   I2S_NUM_1

// recording buffer in PSRAM
// 16kHz * 2 bytes * MAX_RECORD_SECONDS + 44 byte WAV header
static uint8_t* g_rec_buf = nullptr;
static const size_t REC_BUF_BYTES =
    (size_t)MIC_SAMPLE_RATE * 2 * MAX_RECORD_SECONDS + 64;

void audio_begin() {
    // ---------- MIC: I2S0 RX from INMP441 ----------
    i2s_config_t mic_cfg = {};
    mic_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    mic_cfg.sample_rate = MIC_SAMPLE_RATE;
    mic_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;  // INMP441 outputs 32-bit
    mic_cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;   // L/R pin tied LOW = left
    mic_cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);
    mic_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    mic_cfg.dma_buf_count = 8;
    mic_cfg.dma_buf_len = 256;
    mic_cfg.use_apll = false;

    i2s_pin_config_t mic_pins = {};
    mic_pins.bck_io_num = PIN_MIC_SCK;
    mic_pins.ws_io_num = PIN_MIC_WS;
    mic_pins.data_out_num = I2S_PIN_NO_CHANGE;
    mic_pins.data_in_num = PIN_MIC_SD;
    mic_pins.mck_io_num = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_MIC_PORT, &mic_cfg, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &mic_pins);

    // ---------- SPEAKER: I2S1 TX to MAX98357A ----------
    i2s_config_t spk_cfg = {};
    spk_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    spk_cfg.sample_rate = SPK_SAMPLE_RATE;
    spk_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    spk_cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    spk_cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);
    spk_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    spk_cfg.dma_buf_count = 6;
    spk_cfg.dma_buf_len = 512;
    spk_cfg.use_apll = true;

    i2s_pin_config_t spk_pins = {};
    spk_pins.bck_io_num = PIN_SPK_BCLK;
    spk_pins.ws_io_num = PIN_SPK_LRC;
    spk_pins.data_out_num = PIN_SPK_DIN;
    spk_pins.data_in_num = I2S_PIN_NO_CHANGE;
    spk_pins.mck_io_num = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_SPK_PORT, &spk_cfg, 0, NULL);
    i2s_set_pin(I2S_SPK_PORT, &spk_pins);
    i2s_zero_dma_buffer(I2S_SPK_PORT);

    // allocate recording buffer in PSRAM
    g_rec_buf = (uint8_t*)heap_caps_malloc(REC_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (!g_rec_buf) {
        Serial.println("[audio] FAILED to allocate PSRAM recording buffer");
    }
}

// INMP441 is 24-bit in 32-bit container. shift down and clip to 16-bit.
bool audio_mic_read(int16_t* buf, size_t samples) {
    static int32_t raw[512];
    size_t got_total = 0;
    while (got_total < samples) {
        size_t want = min((size_t)512, samples - got_total);
        size_t bytes_read = 0;
        esp_err_t r = i2s_read(I2S_MIC_PORT, raw, want * sizeof(int32_t),
                               &bytes_read, portMAX_DELAY);
        if (r != ESP_OK) return false;
        size_t got = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < got; i++) {
            // INMP441: data is in top 24 bits, so shift down 14 to get ~16-bit range
            // tune this number up if recordings are too quiet, down if clipping
            int32_t s = raw[i] >> 14;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            buf[got_total + i] = (int16_t)s;
        }
        got_total += got;
    }
    return true;
}

// --- recording with simple energy-based silence detection -----------------
static uint32_t frame_energy(const int16_t* frame, size_t n) {
    uint32_t e = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t s = frame[i];
        e += (s * s) >> 8;
    }
    return e / n;
}

uint8_t* audio_record_to_wav(size_t* out_len) {
    if (!g_rec_buf) { *out_len = 0; return nullptr; }

    // WAV header takes 44 bytes; PCM samples start at offset 44
    int16_t* samples = (int16_t*)(g_rec_buf + 44);
    const size_t max_samples = (REC_BUF_BYTES - 44) / 2;
    const size_t frame = 320;   // 20ms at 16kHz
    size_t total = 0;
    uint32_t silence_ms = 0;
    bool had_speech = false;

    while (total + frame < max_samples) {
        if (!audio_mic_read(samples + total, frame)) break;
        uint32_t e = frame_energy(samples + total, frame);
        // threshold — tune for your environment. higher = needs louder voice
        bool is_speech = (e > 2000);
        if (is_speech) {
            had_speech = true;
            silence_ms = 0;
        } else if (had_speech) {
            silence_ms += 20;
        }
        total += frame;
        // stop if extended silence after speech
        if (had_speech && silence_ms > SILENCE_MS_TO_STOP) break;
        // hard cap
        if (total >= MIC_SAMPLE_RATE * MAX_RECORD_SECONDS) break;
    }

    // write WAV header (16-bit PCM mono @ MIC_SAMPLE_RATE)
    const uint32_t data_bytes = total * 2;
    const uint32_t file_bytes = 36 + data_bytes;
    uint8_t* h = g_rec_buf;
    memcpy(h, "RIFF", 4);
    h[4]=file_bytes&0xff; h[5]=(file_bytes>>8)&0xff; h[6]=(file_bytes>>16)&0xff; h[7]=(file_bytes>>24)&0xff;
    memcpy(h+8, "WAVEfmt ", 8);
    h[16]=16; h[17]=0; h[18]=0; h[19]=0;     // fmt chunk size
    h[20]=1;  h[21]=0;                        // PCM
    h[22]=1;  h[23]=0;                        // 1 channel
    uint32_t sr = MIC_SAMPLE_RATE;
    h[24]=sr&0xff; h[25]=(sr>>8)&0xff; h[26]=(sr>>16)&0xff; h[27]=(sr>>24)&0xff;
    uint32_t br = sr * 2;
    h[28]=br&0xff; h[29]=(br>>8)&0xff; h[30]=(br>>16)&0xff; h[31]=(br>>24)&0xff;
    h[32]=2; h[33]=0;                         // block align
    h[34]=16; h[35]=0;                        // bits per sample
    memcpy(h+36, "data", 4);
    h[40]=data_bytes&0xff; h[41]=(data_bytes>>8)&0xff;
    h[42]=(data_bytes>>16)&0xff; h[43]=(data_bytes>>24)&0xff;

    *out_len = 44 + data_bytes;
    return g_rec_buf;
}

void audio_speaker_write(const int16_t* samples, size_t count) {
    size_t bytes_written = 0;
    i2s_write(I2S_SPK_PORT, samples, count * 2, &bytes_written, portMAX_DELAY);
}

void audio_speaker_silence() {
    int16_t silence[256] = {0};
    for (int i = 0; i < 4; i++) audio_speaker_write(silence, 256);
}

void audio_speaker_play_wav(const uint8_t* wav, size_t len) {
    if (len < 44) return;
    // parse minimal WAV header
    uint32_t sample_rate =
        (uint32_t)wav[24] | ((uint32_t)wav[25] << 8) |
        ((uint32_t)wav[26] << 16) | ((uint32_t)wav[27] << 24);
    uint16_t bits = (uint16_t)wav[34] | ((uint16_t)wav[35] << 8);
    uint16_t channels = (uint16_t)wav[22] | ((uint16_t)wav[23] << 8);

    // re-tune I2S rate to match the WAV (Groq returns 48kHz but be safe)
    if (sample_rate != SPK_SAMPLE_RATE) {
        i2s_set_sample_rates(I2S_SPK_PORT, sample_rate);
    }

    // find 'data' chunk (in case there are extra chunks before it)
    size_t pos = 12;
    while (pos + 8 <= len) {
        if (memcmp(wav + pos, "data", 4) == 0) {
            pos += 8;
            break;
        }
        uint32_t chunk_size = (uint32_t)wav[pos+4] | ((uint32_t)wav[pos+5] << 8) |
                              ((uint32_t)wav[pos+6] << 16) | ((uint32_t)wav[pos+7] << 24);
        pos += 8 + chunk_size;
    }
    if (pos >= len) return;

    // play. if stereo, downmix to mono on the fly.
    if (channels == 1 && bits == 16) {
        const int16_t* samples = (const int16_t*)(wav + pos);
        size_t count = (len - pos) / 2;
        audio_speaker_write(samples, count);
    } else if (channels == 2 && bits == 16) {
        const int16_t* samples = (const int16_t*)(wav + pos);
        size_t count = (len - pos) / 4;
        static int16_t mono[512];
        for (size_t i = 0; i < count; i += 512) {
            size_t n = min((size_t)512, count - i);
            for (size_t j = 0; j < n; j++) {
                mono[j] = (samples[(i + j) * 2] + samples[(i + j) * 2 + 1]) / 2;
            }
            audio_speaker_write(mono, n);
        }
    }
    audio_speaker_silence();
}
