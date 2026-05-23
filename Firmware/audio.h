// audio.h — I2S mic capture + speaker playback + recording buffer
#pragma once
#include <Arduino.h>

void audio_begin();

// MIC --------------------------------------------------------------
// reads exactly `samples` 16-bit mono samples from INMP441 into buf.
// blocks until done. returns true on success.
bool audio_mic_read(int16_t* buf, size_t samples);

// records audio into PSRAM until silence or timeout.
// returns pointer to WAV-formatted data (in PSRAM) and sets out_len.
// caller must NOT free; buffer is owned by audio module.
uint8_t* audio_record_to_wav(size_t* out_len);

// SPEAKER ----------------------------------------------------------
// write PCM samples to speaker. blocks until written.
// samples are 16-bit signed mono at SPK_SAMPLE_RATE.
void audio_speaker_write(const int16_t* samples, size_t count);

// play a WAV file from a memory buffer (parses header, plays PCM).
void audio_speaker_play_wav(const uint8_t* wav_data, size_t len);

// helpers
void audio_speaker_silence();   // flush + brief silence to avoid pop
