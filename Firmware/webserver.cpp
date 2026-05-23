// webserver.cpp — phone-facing on/off toggle served from the ESP32
#include "webserver.h"
#include "config.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(WEB_SERVER_PORT);
static volatile bool g_on = true;

// inline page. styled minimal but cute.
static const char PAGE_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bunbun</title>
<style>
:root { color-scheme: light dark; }
html, body { height: 100%; margin: 0; }
body {
  font-family: ui-rounded, -apple-system, "Segoe UI", sans-serif;
  background: radial-gradient(circle at 30% 20%, #fde4ef, #fff2f7 60%, #fdd8e8);
  display: flex; flex-direction: column; align-items: center; justify-content: center;
  -webkit-tap-highlight-color: transparent;
  color: #3a1e2c;
}
h1 { font-weight: 700; font-size: clamp(28px, 7vw, 44px); margin: 0 0 4px; letter-spacing: -0.5px; }
.sub { opacity: 0.6; font-size: 14px; margin-bottom: 28px; }
.toggle {
  appearance: none; border: 0;
  width: 220px; height: 220px; border-radius: 50%;
  background: linear-gradient(145deg, #ff9ec3, #ff5d97);
  color: white; font-size: 28px; font-weight: 800; letter-spacing: 1px;
  box-shadow: 0 18px 40px -10px rgba(255, 93, 151, 0.6),
              inset 0 -8px 18px rgba(0,0,0,0.12),
              inset 0 6px 16px rgba(255,255,255,0.35);
  cursor: pointer; transition: transform .12s ease, filter .15s ease, background .25s;
  user-select: none;
}
.toggle:active { transform: scale(0.97); }
.toggle.off {
  background: linear-gradient(145deg, #bcbcbc, #6b6b6b);
  box-shadow: 0 18px 30px -10px rgba(0,0,0,0.35),
              inset 0 -8px 18px rgba(0,0,0,0.18),
              inset 0 6px 16px rgba(255,255,255,0.18);
}
.state { margin-top: 28px; font-size: 16px; opacity: 0.75; }
.dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%;
       background: #34c759; margin-right: 6px; vertical-align: middle; }
.off .dot { background: #888; }
footer { position: fixed; bottom: 12px; font-size: 11px; opacity: 0.4; }
</style>
</head>
<body>
  <h1>Bunbun</h1>
  <div class="sub">tap to wake or send to sleep</div>
  <button class="toggle" id="t">ON</button>
  <div class="state" id="s"><span class="dot"></span>listening for "hey bunny"</div>
  <footer>esp32 · bunny clock</footer>
<script>
const t = document.getElementById('t'), s = document.getElementById('s');
function render(on) {
  t.textContent = on ? 'ON' : 'OFF';
  t.classList.toggle('off', !on);
  s.classList.toggle('off', !on);
  s.innerHTML = on
    ? '<span class="dot"></span>listening for "hey bunny"'
    : '<span class="dot"></span>sleeping. tap to wake.';
}
async function poll() {
  try { const r = await fetch('/state'); const j = await r.json(); render(j.on); }
  catch(e) {}
}
t.addEventListener('click', async () => {
  const next = t.textContent === 'OFF';
  render(next);
  try { await fetch('/toggle?on=' + (next ? '1' : '0'), {method:'POST'}); } catch(e) {}
});
poll(); setInterval(poll, 4000);
</script>
</body>
</html>
)HTML";

void webserver_begin() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", PAGE_HTML);
    });
    server.on("/state", HTTP_GET, [](AsyncWebServerRequest* req) {
        char j[24];
        snprintf(j, sizeof(j), "{\"on\":%s}", g_on ? "true" : "false");
        req->send(200, "application/json", j);
    });
    server.on("/toggle", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (req->hasParam("on")) {
            g_on = (req->getParam("on")->value() == "1");
        } else {
            g_on = !g_on;
        }
        char j[24];
        snprintf(j, sizeof(j), "{\"on\":%s}", g_on ? "true" : "false");
        req->send(200, "application/json", j);
    });
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "not here");
    });
    server.begin();
    Serial.printf("[web] phone control: http://%s/\n", WiFi.localIP().toString().c_str());
}

bool webserver_is_on() { return g_on; }
