// groq.cpp — Groq Cloud client: Whisper STT, Llama LLM, PlayAI TTS
#include "groq.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// For prototyping we accept any cert. To pin the real Groq root CA later,
// replace setInsecure() with setCACert(GROQ_ROOT_CA).
static void make_client(WiFiClientSecure& c) {
    c.setInsecure();
    c.setTimeout(30000);
}

// ---------- Conversation history (kept small to fit in RAM + tokens) -------
#define MAX_HISTORY 6
struct Msg { String role; String content; };
static Msg g_history[MAX_HISTORY];
static int g_history_count = 0;

static void push_history(const String& role, const String& content) {
    if (g_history_count >= MAX_HISTORY) {
        // shift out oldest (keep at least 1 user/assistant pair structure)
        for (int i = 1; i < MAX_HISTORY; i++) g_history[i-1] = g_history[i];
        g_history_count = MAX_HISTORY - 1;
    }
    g_history[g_history_count].role = role;
    g_history[g_history_count].content = content;
    g_history_count++;
}

void groq_llm_reset_history() { g_history_count = 0; }

// ============================================================================
// 1) STT — POST audio/wav to /openai/v1/audio/transcriptions
// ============================================================================
String groq_stt_transcribe(const uint8_t* wav, size_t wav_len) {
    WiFiClientSecure client;
    make_client(client);
    if (!client.connect(GROQ_HOST, 443)) {
        Serial.println("[stt] connect failed");
        return "";
    }

    // multipart boundary
    const char* boundary = "----bunny42boundary";
    String head =
        "--" + String(boundary) + "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
        + GROQ_STT_MODEL + "\r\n"
        + "--" + boundary + "\r\n"
        + "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
        + "text\r\n"
        + "--" + boundary + "\r\n"
        + "Content-Disposition: form-data; name=\"file\"; filename=\"a.wav\"\r\n"
        + "Content-Type: audio/wav\r\n\r\n";
    String tail = "\r\n--" + String(boundary) + "--\r\n";

    size_t total_body = head.length() + wav_len + tail.length();

    // request line + headers
    client.printf("POST /openai/v1/audio/transcriptions HTTP/1.1\r\n");
    client.printf("Host: %s\r\n", GROQ_HOST);
    client.printf("Authorization: Bearer %s\r\n", GROQ_API_KEY);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
    client.printf("Content-Length: %u\r\n", (unsigned)total_body);
    client.printf("Connection: close\r\n\r\n");

    // body
    client.print(head);
    // write WAV in chunks to avoid huge TCP push
    const size_t chunk = 1024;
    for (size_t off = 0; off < wav_len; off += chunk) {
        size_t n = min(chunk, wav_len - off);
        client.write(wav + off, n);
    }
    client.print(tail);

    // wait for response
    uint32_t t0 = millis();
    while (client.connected() && !client.available() && millis() - t0 < 30000) {
        delay(10);
    }
    // read status line
    String status = client.readStringUntil('\n');
    if (status.indexOf(" 200 ") < 0) {
        Serial.printf("[stt] HTTP not 200: %s\n", status.c_str());
        // drain
        while (client.available()) client.read();
        return "";
    }
    // skip headers
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() <= 1) break;
    }
    // body = plain text (because response_format=text)
    String body;
    while (client.connected() || client.available()) {
        while (client.available()) body += (char)client.read();
        delay(1);
    }
    body.trim();
    return body;
}

// ============================================================================
// 2) LLM — POST to /openai/v1/chat/completions
// ============================================================================
String groq_llm_chat(const String& user_text) {
    push_history("user", user_text);

    // Build JSON body
    JsonDocument doc;
    doc["model"] = GROQ_LLM_MODEL;
    doc["max_tokens"] = 120;       // keep replies short (TTS-friendly)
    doc["temperature"] = 0.9;
    JsonArray msgs = doc["messages"].to<JsonArray>();
    JsonObject sys = msgs.add<JsonObject>();
    sys["role"] = "system";
    sys["content"] = BUNNY_PERSONALITY;
    for (int i = 0; i < g_history_count; i++) {
        JsonObject m = msgs.add<JsonObject>();
        m["role"] = g_history[i].role;
        m["content"] = g_history[i].content;
    }
    String body;
    serializeJson(doc, body);

    WiFiClientSecure client;
    make_client(client);
    HTTPClient http;
    if (!http.begin(client, "https://api.groq.com/openai/v1/chat/completions")) {
        Serial.println("[llm] http.begin failed");
        return "";
    }
    http.addHeader("Authorization", "Bearer " GROQ_API_KEY);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("[llm] HTTP %d\n", code);
        http.end();
        return "";
    }
    String resp = http.getString();
    http.end();

    JsonDocument rdoc;
    DeserializationError e = deserializeJson(rdoc, resp);
    if (e) {
        Serial.printf("[llm] JSON err: %s\n", e.c_str());
        return "";
    }
    String reply = rdoc["choices"][0]["message"]["content"].as<String>();
    reply.trim();
    push_history("assistant", reply);
    return reply;
}

// ============================================================================
// 3) TTS — POST to /openai/v1/audio/speech, returns WAV bytes
// ============================================================================
// out_buf is a static PSRAM buffer reused across calls. Don't free.
static uint8_t* g_tts_buf = nullptr;
static const size_t TTS_BUF_MAX = 600 * 1024;   // ~6s of 48kHz mono 16-bit

size_t groq_tts_speak(const String& text, uint8_t** out_buf) {
    if (!g_tts_buf) {
        g_tts_buf = (uint8_t*)heap_caps_malloc(TTS_BUF_MAX, MALLOC_CAP_SPIRAM);
    }
    if (!g_tts_buf) { *out_buf = nullptr; return 0; }

    JsonDocument doc;
    doc["model"] = GROQ_TTS_MODEL;
    doc["voice"] = GROQ_TTS_VOICE;
    doc["input"] = text;
    doc["response_format"] = "wav";
    String body;
    serializeJson(doc, body);

    WiFiClientSecure client;
    make_client(client);
    HTTPClient http;
    if (!http.begin(client, "https://api.groq.com/openai/v1/audio/speech")) {
        Serial.println("[tts] http.begin failed");
        *out_buf = nullptr; return 0;
    }
    http.addHeader("Authorization", "Bearer " GROQ_API_KEY);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("[tts] HTTP %d\n", code);
        Serial.println(http.getString());   // print error
        http.end();
        *out_buf = nullptr; return 0;
    }

    // stream response straight into PSRAM buffer
    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    while (http.connected() && total < TTS_BUF_MAX) {
        size_t avail = stream->available();
        if (avail) {
            size_t want = min(avail, TTS_BUF_MAX - total);
            int got = stream->readBytes(g_tts_buf + total, want);
            if (got <= 0) break;
            total += got;
        } else {
            delay(1);
            // exit if no data for a while AND we've already got some
            static uint32_t last;
            if (total > 0 && millis() - last > 2000) break;
            last = millis();
        }
    }
    http.end();
    *out_buf = g_tts_buf;
    return total;
}
