// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// web.cpp — the setup portal and OTA.
//
// Every handler runs in LOOP context via webLoop() -> handleClient(), so NVS
// and LVGL access are legal here and nowhere else.
//
// Two security rules are carried verbatim from AirRadar, and each cost a real
// debugging session (INHERITED_RULES.md §20-21):
//
//   * A blank secret field must NEVER overwrite a stored secret. The field
//     renders blank by design so the secret is not served back, so an
//     unconditional write means saving any unrelated setting erases it.
//
//   * Comparing Origin to Host is NOT a guard. Both are the requester's to
//     choose, so the test only ever proves they agree — and under DNS
//     rebinding they agree perfectly. Validate Host against the names this
//     device actually answers to FIRST; that is the anchor the origin test
//     hangs off.
#include "web.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "../core/state.h"
#include "../ui/ui.h"

static WebServer s_srv(80);
static bool s_up = false;

static String mdnsName() { return "scoredeck"; }

/** Host must be a name we actually answer to. Absent Host is allowed on
 *  purpose: every browser sends one, so rejecting buys nothing, and a client
 *  that omits it cannot be rebound. */
static bool hostAllowed() {
  const String host = s_srv.hostHeader();
  if (!host.length()) return true;
  String h = host;
  const int colon = h.indexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  h.toLowerCase();
  return h == mdnsName() + ".local" || h == mdnsName() ||
         h == WiFi.localIP().toString() || h == WiFi.softAPIP().toString();
}

/** State-changing requests must also carry an Origin we recognise. */
static bool originAllowed() {
  const String o = s_srv.header("Origin");
  if (!o.length()) return true;            // non-browser client
  const String host = s_srv.hostHeader();
  if (!host.length()) return false;
  return o == "http://" + host || o == "https://" + host;
}

static bool guard() {
  if (!hostAllowed())   { s_srv.send(403, "text/plain", "bad host"); return false; }
  if (!originAllowed()) { s_srv.send(403, "text/plain", "bad origin"); return false; }
  if (g_set.panelPass.length()) {
    if (!s_srv.authenticate("admin", g_set.panelPass.c_str())) {
      s_srv.requestAuthentication();
      return false;
    }
  }
  return true;
}

static String esc(const String& in) {
  String o;
  for (size_t i = 0; i < in.length(); i++) {
    const char c = in[i];
    if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;"; else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static const char kCss[] PROGMEM =
  "<style>"
  ":root{--bg:#0A0F18;--card:#151D28;--edge:#2A3646;--ink:#F3F7FB;--ink2:#93A5B8;--ink3:#5D6D7E}"
  "*{box-sizing:border-box}"
  "body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,sans-serif}"
  ".w{max-width:640px;margin:0 auto;padding:24px 18px 60px}"
  "h1{font-size:26px;letter-spacing:.04em;margin:8px 0 2px}"
  ".sub{color:var(--ink3);font-size:13px;margin-bottom:22px}"
  "form{background:var(--card);border:1px solid var(--edge);border-radius:12px;padding:18px;margin-bottom:18px}"
  "h2{font-size:12px;letter-spacing:.14em;text-transform:uppercase;color:var(--ink3);margin:0 0 14px}"
  "label{display:block;font-size:12px;color:var(--ink2);margin:12px 0 5px}"
  "input,select{width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--edge);"
  "background:#0E1620;color:var(--ink);font-size:15px}"
  "button{margin-top:16px;padding:11px 18px;border:0;border-radius:8px;background:#2A3646;"
  "color:var(--ink);font-size:15px;cursor:pointer}"
  "button:hover{background:#36455a}"
  ".row{display:flex;gap:10px}.row>*{flex:1}"
  ".hint{color:var(--ink3);font-size:12px;margin-top:6px}"
  ".ok{color:#7FD4A0}"
  "</style>";

static void pageRoot() {
  if (!guard()) return;
  String h;
  h.reserve(4500);
  h += F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>");
  h += F("<title>ScoreDeck</title>");
  h += FPSTR(kCss);
  h += F("<div class=w><h1>ScoreDeck</h1><div class=sub>");
  h += SD_VERSION;
  h += F(" &middot; ");
  h += WiFi.localIP().toString();
  h += F("</div>");

  h += F("<form method=post action=/save><h2>Proxy</h2>"
         "<label>Proxy URL</label><input name=proxy value='");
  h += esc(g_set.proxy);
  h += F("' placeholder='http://192.168.1.50:8787'>"
         "<label>Token</label><input name=token type=password placeholder='");
  h += g_set.token.length() ? F("unchanged") : F("none set");
  h += F("'><div class=hint>Leave blank to keep the stored token.</div>"
         "<div class=row><div><label>Region</label><select name=rgn>");
  static const char* kRegions[] = { "us","ca","gb","ie","au","nz","de","fr","es","it","mx","in" };
  for (const char* r : kRegions) {
    h += F("<option");
    if (g_set.region == r) h += F(" selected");
    h += F(">"); h += r; h += F("</option>");
  }
  h += F("</select></div><div><label>Timezone (POSIX TZ)</label><input name=tz value='");
  h += esc(g_set.tz);
  h += F("'></div></div><button>Save</button></form>");

  h += F("<form method=post action=/save><h2>Teams &amp; leagues</h2>"
         "<label>Favourites</label><input name=favs value='");
  h += esc(g_set.favs);
  h += F("' placeholder='nhl:21,mlb:14'>"
         "<div class=hint>league:teamId, comma separated. Look ids up at "
         "<code>/v1/teams/&lt;league&gt;</code> on your proxy.</div>"
         "<label>Leagues</label><input name=lgs value='");
  h += esc(g_set.leagues);
  h += F("' placeholder='nhl,nfl,nba,mlb'>"
         "<label>Density</label><select name=dens>");
  static const char* kDens[] = { "Roomy (6)", "Standard (9)", "Dense (12)" };
  for (uint8_t i = 0; i < 3; i++) {
    h += F("<option value="); h += String(i);
    if (g_set.density == i) h += F(" selected");
    h += F(">"); h += kDens[i]; h += F("</option>");
  }
  h += F("</select><button>Save</button></form>");

  h += F("<form method=post action=/save><h2>Alerts &amp; quiet hours</h2>"
         "<label>Score alerts</label><select name=alen><option value=1");
  if (g_set.alertsOn) h += F(" selected");
  h += F(">On</option><option value=0");
  if (!g_set.alertsOn) h += F(" selected");
  h += F(">Off</option></select>"
         "<label>Quiet hours</label><select name=qen><option value=1");
  if (g_set.quietOn) h += F(" selected");
  h += F(">On</option><option value=0");
  if (!g_set.quietOn) h += F(" selected");
  h += F(">Off</option></select>"
         "<div class=row><div><label>From (HH:MM)</label><input name=qfr value='");
  char t[8];
  snprintf(t, sizeof t, "%02u:%02u", g_set.quietFrom / 60, g_set.quietFrom % 60);
  h += t;
  h += F("'></div><div><label>To (HH:MM)</label><input name=qto value='");
  snprintf(t, sizeof t, "%02u:%02u", g_set.quietTo / 60, g_set.quietTo % 60);
  h += t;
  h += F("'></div></div><button>Save</button></form>");

  h += F("<form method=post action=/save><h2>Wi-Fi &amp; access</h2>"
         "<label>Network</label><input name=ssid value='");
  h += esc(g_set.ssid);
  h += F("'><label>Password</label><input name=pass type=password placeholder='unchanged'>"
         "<label>Panel password (blank = no login)</label>"
         "<input name=ppass type=password placeholder='");
  h += g_set.panelPass.length() ? F("set") : F("none");
  h += F("'><div class=hint>Saving Wi-Fi reboots the panel.</div><button>Save</button></form>");

  h += F("<form method=post action=/update enctype='multipart/form-data'>"
         "<h2>Firmware</h2><input type=file name=f accept='.bin'>"
         "<button>Upload &amp; restart</button></form>");
  h += F("</div>");
  s_srv.send(200, "text/html", h);
}

static uint16_t parseHHMM(const String& v, uint16_t fallback) {
  const int c = v.indexOf(':');
  if (c < 1) return fallback;
  const int hh = v.substring(0, c).toInt();
  const int mm = v.substring(c + 1).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return fallback;
  return (uint16_t)(hh * 60 + mm);
}

static void pageSave() {
  if (!guard()) return;
  bool wifiChanged = false;

  if (s_srv.hasArg("proxy")) g_set.proxy = s_srv.arg("proxy");
  if (s_srv.hasArg("rgn"))   g_set.region = s_srv.arg("rgn");
  if (s_srv.hasArg("tz"))    g_set.tz = s_srv.arg("tz");
  if (s_srv.hasArg("favs"))  g_set.favs = s_srv.arg("favs").substring(0, FAVS_MAX_LEN);
  if (s_srv.hasArg("lgs"))   g_set.leagues = s_srv.arg("lgs").substring(0, FAVS_MAX_LEN);
  if (s_srv.hasArg("dens"))  g_set.density = (uint8_t)constrain(s_srv.arg("dens").toInt(), 0, 2);
  if (s_srv.hasArg("alen"))  g_set.alertsOn = s_srv.arg("alen").toInt() != 0;
  if (s_srv.hasArg("qen"))   g_set.quietOn = s_srv.arg("qen").toInt() != 0;
  if (s_srv.hasArg("qfr"))   g_set.quietFrom = parseHHMM(s_srv.arg("qfr"), g_set.quietFrom);
  if (s_srv.hasArg("qto"))   g_set.quietTo = parseHHMM(s_srv.arg("qto"), g_set.quietTo);
  if (s_srv.hasArg("ssid") && s_srv.arg("ssid") != g_set.ssid) {
    g_set.ssid = s_srv.arg("ssid");
    wifiChanged = true;
  }

  // Blank secrets are "unchanged", never "erase". INHERITED_RULES.md §20.
  if (s_srv.hasArg("pass")  && s_srv.arg("pass").length())  { g_set.pass = s_srv.arg("pass"); wifiChanged = true; }
  if (s_srv.hasArg("token") && s_srv.arg("token").length())   g_set.token = s_srv.arg("token");
  if (s_srv.hasArg("ppass") && s_srv.arg("ppass").length())   g_set.panelPass = s_srv.arg("ppass");

  settingsSave();
  if (g_set.tz.length()) { setenv("TZ", g_set.tz.c_str(), 1); tzset(); }
  uiInit();
  uiBoardRefresh();

  s_srv.sendHeader("Location", "/");
  s_srv.send(303, "text/plain", "saved");
  if (wifiChanged) { delay(200); ESP.restart(); }
}

static void pageUpdate() {
  s_srv.sendHeader("Connection", "close");
  const bool ok = !Update.hasError();
  s_srv.send(200, "text/plain", ok ? "OK, restarting" : "FAILED");
  delay(300);
  if (ok) ESP.restart();
}

static void pageUpload() {
  HTTPUpload& up = s_srv.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!hostAllowed() || !originAllowed()) return;
    if (g_set.panelPass.length() && !s_srv.authenticate("admin", g_set.panelPass.c_str())) return;
    Serial.printf("[web] OTA start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      Serial.printf("[web] OTA begin failed: %s\n", Update.errorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize)
      Serial.printf("[web] OTA write failed: %s\n", Update.errorString());
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[web] OTA done: %u bytes\n", up.totalSize);
    else Serial.printf("[web] OTA end failed: %s\n", Update.errorString());
  }
}

static void pageState() {
  if (!guard()) return;
  String j = "{\"v\":\"" SD_VERSION "\",\"games\":";
  j += String(g_gameCount);
  j += ",\"net\":" + String((int)g_net);
  j += ",\"heap\":" + String((unsigned)ESP.getFreeHeap());
  j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  j += ",\"proxy\":" + String(g_set.proxy.length() ? "true" : "false");
  j += "}";
  s_srv.send(200, "application/json", j);
}

void webBegin() {
  if (s_up) return;
  if (MDNS.begin(mdnsName().c_str())) MDNS.addService("http", "tcp", 80);
  const char* wanted[] = { "Origin" };
  s_srv.collectHeaders(wanted, 1);
  s_srv.on("/", HTTP_GET, pageRoot);
  s_srv.on("/save", HTTP_POST, pageSave);
  s_srv.on("/api/state", HTTP_GET, pageState);
  s_srv.on("/update", HTTP_POST, pageUpdate, pageUpload);
  s_srv.onNotFound([] { s_srv.send(404, "text/plain", "not found"); });
  s_srv.begin();
  s_up = true;
  Serial.printf("[web] portal on http://%s/ (also http://%s.local/)\n",
                WiFi.localIP().toString().c_str(), mdnsName().c_str());
}

void webLoop() { if (s_up) s_srv.handleClient(); }
bool webUp() { return s_up; }
