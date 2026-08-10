// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// web.cpp — the browser portal, OTA and diagnostics.
//
// Every handler runs in LOOP context via webLoop() -> handleClient(), so NVS
// and LVGL access are legal here and nowhere else. It also means response time
// IS panel stall time, which is why the page is streamed from PROGMEM instead
// of being assembled into a String.
//
// Two security rules are carried verbatim from AirRadar, and each cost a real
// debugging session (INHERITED_RULES.md §20-21):
//
//   * A blank secret field must NEVER overwrite a stored secret. The field
//     renders blank by design so the secret is not served back, so an
//     unconditional write means saving any unrelated setting erases it. The
//     corollary is that blank cannot mean "erase" either — so a single "-"
//     does, and without it a token set by mistake could only be removed by a
//     factory reset.
//
//   * Comparing Origin to Host is NOT a guard. Both are the requester's to
//     choose, so the test only ever proves they agree — and under DNS
//     rebinding they agree perfectly. Validate Host against the names this
//     device actually answers to FIRST; that is the anchor the origin test
//     hangs off.
#include "web.h"
#include "portal.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "../core/state.h"
#include "../net/api.h"
#include "../ui/ui.h"

static WebServer s_srv(80);
static bool s_up = false;
static bool s_otaAuthFailed = false;

static String mdnsName() { return "scoredeck"; }

// ── guards ─────────────────────────────────────────────────────────────────

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
         h == WiFi.localIP().toString();
}

/** State-changing requests must also carry an Origin we recognise. */
static bool originAllowed() {
  String o = s_srv.header("Origin");
  if (!o.length()) return true;              // non-browser client
  const String host = s_srv.hostHeader();
  if (!host.length()) return false;
  // Case-insensitively: scheme and host are both case-insensitive per RFC
  // 3986, and a browser is free to send either. A case-sensitive compare
  // rejects a legitimate request from a user who typed the host in caps.
  String want = "http://" + host;
  if (o.equalsIgnoreCase(want)) return true;
  want = "https://" + host;
  return o.equalsIgnoreCase(want);
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

static void noStore() {
  s_srv.sendHeader("Cache-Control", "no-store");
  s_srv.sendHeader("X-Content-Type-Options", "nosniff");
  s_srv.sendHeader("Referrer-Policy", "no-referrer");
}

static void sendJson(const String& body, int code = 200) {
  noStore();
  s_srv.send(code, "application/json", body);
}

static void fail(int code, const char* why) {
  String j = "{\"error\":\"";
  j += why;
  j += "\"}";
  sendJson(j, code);
}

/** JSON string escape. Team names and SSIDs are arbitrary upstream text. */
static String jstr(const String& in) {
  String o = "\"";
  for (size_t i = 0; i < in.length(); i++) {
    const char c = in[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((unsigned char)c < 0x20) continue;      // drop control bytes
    else o += c;
  }
  o += '"';
  return o;
}

// ── body parsing ───────────────────────────────────────────────────────────
//
// The portal posts JSON. Rather than pull in a parser for six flat fields,
// pick them out by key — the shapes are fixed and authored in one place.

static String bodyOf() { return s_srv.hasArg("plain") ? s_srv.arg("plain") : String(); }

static bool jsonField(const String& body, const char* key, String& out) {
  String pat = "\"";
  pat += key;
  pat += "\"";
  int i = body.indexOf(pat);
  if (i < 0) return false;
  i = body.indexOf(':', i + pat.length());
  if (i < 0) return false;
  i++;
  while (i < (int)body.length() && isspace((unsigned char)body[i])) i++;
  if (i >= (int)body.length()) return false;
  if (body[i] == '"') {
    const int end = body.indexOf('"', i + 1);
    if (end < 0) return false;
    out = body.substring(i + 1, end);
  } else {
    int end = i;
    while (end < (int)body.length() && body[end] != ',' && body[end] != '}') end++;
    out = body.substring(i, end);
    out.trim();
  }
  return true;
}

static bool jsonInt(const String& body, const char* key, long& out) {
  String v;
  if (!jsonField(body, key, v)) return false;
  out = v.toInt();
  return true;
}

// ── validation ─────────────────────────────────────────────────────────────
//
// Validate EVERYTHING, then apply. The previous version applied field by
// field, so a bad quiet-hour still committed a new proxy URL — a partial write
// to NVS is the worst outcome available.

static bool validFavs(const String& v, String& why) {
  if (v.length() > FAVS_MAX_LEN) { why = "favourites list too long"; return false; }
  if (!v.length()) return true;
  int start = 0, count = 0;
  while (start <= (int)v.length()) {
    int comma = v.indexOf(',', start);
    if (comma < 0) comma = v.length();
    const String e = v.substring(start, comma);
    if (!e.length()) { why = "empty entry in the favourites list"; return false; }
    const int colon = e.indexOf(':');
    if (colon <= 0 || colon == (int)e.length() - 1) {
      why = "each favourite must look like league:teamId";
      return false;
    }
    for (int i = 0; i < colon; i++) {
      const char c = e[i];
      if (!(isalnum((unsigned char)c) || c == '.')) { why = "bad league in a favourite"; return false; }
    }
    for (int i = colon + 1; i < (int)e.length(); i++) {
      if (!isdigit((unsigned char)e[i])) { why = "team id must be digits"; return false; }
    }
    if (++count > FAVS_MAX) { why = "at most 20 favourites"; return false; }
    if (comma >= (int)v.length()) break;
    start = comma + 1;
  }
  return true;
}

/** Blank keeps, "-" clears, anything else sets. See the file header. */
static void applySecret(String& dst, const String& v) {
  if (!v.length()) return;
  if (v == "-") { dst = ""; return; }
  dst = v;
}

// ── routes ─────────────────────────────────────────────────────────────────

static void pageRoot() {
  if (!guard()) return;
  noStore();
  // Neutralises any upstream string that ever escapes escaping, and stops the
  // page reaching anything but this device and the configured proxy.
  String csp = "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
               "img-src 'self' data:; form-action 'self'; frame-ancestors 'none'; base-uri 'none'; "
               "connect-src 'self'";
  if (g_set.proxy.length()) { csp += " "; csp += g_set.proxy; }
  s_srv.sendHeader("Content-Security-Policy", csp);
  s_srv.setContentLength(strlen_P(PORTAL_HTML));
  s_srv.send(200, "text/html; charset=utf-8", "");
  // 1 KB at a time: the whole point of PROGMEM here is to never hold the page
  // in RAM, and it bounds how long any single write blocks the display.
  const char* p = PORTAL_HTML;
  size_t left = strlen_P(PORTAL_HTML);
  char buf[1024];
  while (left) {
    const size_t n = left < sizeof buf ? left : sizeof buf;
    memcpy_P(buf, p, n);
    s_srv.sendContent(buf, n);
    p += n;
    left -= n;
  }
}

static void apiConfigGet() {
  if (!guard()) return;
  String j = "{";
  j += "\"v\":" + jstr(SD_VERSION);
  j += ",\"host\":" + jstr(mdnsName() + ".local");
  j += ",\"ip\":" + jstr(WiFi.localIP().toString());
  j += ",\"ssid\":" + jstr(g_set.ssid);
  j += ",\"proxy\":" + jstr(g_set.proxy);
  // Secrets are reported as booleans, never echoed. The token is the one
  // exception and it is deliberate — see apiToken() below.
  j += ",\"hasToken\":" + String(g_set.token.length() ? "true" : "false");
  j += ",\"hasPass\":" + String(g_set.panelPass.length() ? "true" : "false");
  j += ",\"rgn\":" + jstr(g_set.region);
  j += ",\"tz\":" + jstr(g_set.tz);
  j += ",\"tzi\":" + jstr(tzForProxy());
  // The curated table, so the browser offers the same cities the panel does
  // and neither can pick something the other cannot honour.
  j += ",\"tzs\":[";
  for (uint8_t i = 0; i < kTimeZoneCount; i++) {
    if (i) j += ',';
    j += "[" + jstr(kTimeZones[i].label) + "," + jstr(kTimeZones[i].iana) + "]";
  }
  j += "]";
  j += ",\"favs\":" + jstr(g_set.favs);
  j += ",\"dens\":" + String(g_set.density);
  j += ",\"alen\":" + String(g_set.alertsOn ? 1 : 0);
  j += ",\"clk24\":" + String(g_set.clock24 ? 1 : 0);
  j += ",\"focus\":" + String(g_set.focusOn ? 1 : 0);
  j += ",\"qen\":" + String(g_set.quietOn ? 1 : 0);
  j += ",\"qfr\":" + String(g_set.quietFrom);
  j += ",\"qto\":" + String(g_set.quietTo);
  j += ",\"now\":" + jstr(localClockNow());
  const esp_partition_t* nxt = esp_ota_get_next_update_partition(nullptr);
  j += ",\"slot\":" + String(nxt ? (unsigned)nxt->size : 0u);
  // The portal needs the bearer to fetch the catalog from the proxy. Worth
  // stating plainly rather than hiding: anything that can read this endpoint
  // can already flash arbitrary firmware, so the token is not a new exposure.
  j += ",\"token\":" + jstr(g_set.token);
  j += "}";
  sendJson(j);
}

static void apiConfigPost() {
  if (!guard()) return;
  const String body = bodyOf();

  // ── validate ─────────────────────────────────────────────────────────────
  String why, sProxy, sRgn, sToken, sPass;
  long dens = g_set.density, alen = g_set.alertsOn, focus = g_set.focusOn;
  long qen = g_set.quietOn, qfr = g_set.quietFrom, qto = g_set.quietTo;
  long clk24 = g_set.clock24;

  if (jsonField(body, "proxy", sProxy)) {
    if (sProxy.length() && !sProxy.startsWith("http://") && !sProxy.startsWith("https://"))
      return fail(400, "proxy must start with http:// or https://");
    if (sProxy.length() > 96) return fail(400, "proxy url too long");
  }
  if (jsonField(body, "rgn", sRgn)) {
    if (sRgn.length() != 2) return fail(400, "region must be a two-letter code");
  }
  String sTzi;
  if (jsonField(body, "tzi", sTzi) && sTzi.length() && tzIndexOf(sTzi.c_str()) < 0)
    return fail(400, "unknown time zone");
  jsonInt(body, "dens", dens);
  if (dens < 0 || dens >= DEN_COUNT) return fail(400, "unknown density");
  jsonInt(body, "alen", alen);
  jsonInt(body, "focus", focus);
  jsonInt(body, "clk24", clk24);
  jsonInt(body, "qen", qen);
  jsonInt(body, "qfr", qfr);
  jsonInt(body, "qto", qto);
  if (qfr < 0 || qfr > 1439 || qto < 0 || qto > 1439)
    return fail(400, "quiet hours must be within a day");
  jsonField(body, "token", sToken);
  jsonField(body, "ppass", sPass);

  // ── apply ────────────────────────────────────────────────────────────────
  if (sProxy.length() || jsonField(body, "proxy", why)) g_set.proxy = sProxy;
  if (sRgn.length()) g_set.region = sRgn;
  if (sTzi.length()) tzApply(sTzi.c_str());
  g_set.density  = (uint8_t)dens;
  g_set.alertsOn = alen != 0;
  g_set.focusOn  = focus != 0;
  g_set.clock24  = clk24 != 0;
  g_set.quietOn  = qen != 0;
  g_set.quietFrom = (uint16_t)qfr;
  g_set.quietTo   = (uint16_t)qto;
  applySecret(g_set.token, sToken);
  applySecret(g_set.panelPass, sPass);

  settingsSave();
  if (g_set.tz.length()) { setenv("TZ", g_set.tz.c_str(), 1); tzset(); }
  uiInit();
  uiBoardRefresh();
  sendJson("{\"ok\":true}");
}

static void apiFavsPost() {
  if (!guard()) return;
  String favs, why;
  if (!jsonField(bodyOf(), "favs", favs)) return fail(400, "no favourites in the request");
  // Reject rather than truncate. The old code silently cut the list at 240
  // bytes, which ate the user's last team without telling them.
  if (!validFavs(favs, why)) return fail(400, why.c_str());
  g_set.favs = favs;
  settingsSave();
  sendJson("{\"ok\":true}");
}

static void apiState() {
  if (!guard()) return;
  uint8_t live = 0;
  for (uint8_t i = 0; i < g_gameCount; i++) if (g_board[i].state == GS_LIVE) live++;

  String j = "{\"net\":" + String((int)g_net);
  j += ",\"games\":" + String(g_gameCount);
  j += ",\"live\":" + String(live);
  j += ",\"next\":" + String(webNextPollSecs());
  j += ",\"heap\":" + String((unsigned)ESP.getFreeHeap());
  j += ",\"b\":[";
  for (uint8_t i = 0; i < g_gameCount; i++) {
    const Game& g = g_board[i];
    if (i) j += ',';
    j += "{\"l\":" + jstr(g.league);
    j += ",\"a\":" + jstr(g.away.abbr) + ",\"h\":" + jstr(g.home.abbr);
    j += ",\"as\":" + String(g.away.score) + ",\"hs\":" + String(g.home.score);
    j += ",\"g\":" + String((int)g.state);
    j += ",\"st\":" + jstr(g.status);
    j += ",\"b\":" + jstr(g.bcast);
    char col[8];
    snprintf(col, sizeof col, "%06lX", (unsigned long)(g.away.color & 0xFFFFFF));
    j += ",\"ac\":" + jstr(col);
    if (g.isFav) j += ",\"f\":1";
    j += '}';
  }
  j += "]}";
  sendJson(j);
}

static void apiDiag() {
  if (!guard()) return;
  WebDiag d;
  webCollectDiag(d);
  String j = "{";
  j += "\"heap\":" + String((unsigned)ESP.getFreeHeap());
  j += ",\"largest\":" + String((unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  j += ",\"gate\":" + String(netGateOpen() ? 1 : 0);
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"up\":" + String((unsigned long)(millis() / 1000));
  j += ",\"reset\":" + jstr(d.resetReason);
  j += ",\"sleep\":" + String(WiFi.getSleep() ? 1 : 0);
  j += ",\"pollAge\":" + String(d.pollAgeS);
  j += ",\"pollCode\":" + String(d.pollCode);
  j += ",\"pollMs\":" + String(d.pollMs);
  j += ",\"next\":" + String(webNextPollSecs());
  j += ",\"declGate\":" + String(d.declGate);
  j += ",\"declFlight\":" + String(d.declFlight);
  j += ",\"declNoProxy\":" + String(d.declNoProxy);
  j += ",\"stale\":" + String(d.stale ? 1 : 0);
  j += ",\"seq\":" + String((unsigned long)g_set.lastSeq);
  j += ",\"proxySeq\":" + String((unsigned long)d.proxySeq);
  j += ",\"logoHit\":" + String(d.logoHit);
  j += ",\"logoMiss\":" + String(d.logoMiss);
  j += ",\"games\":" + String(g_gameCount);
  uint8_t live = 0;
  for (uint8_t i = 0; i < g_gameCount; i++) if (g_board[i].state == GS_LIVE) live++;
  j += ",\"live\":" + String(live);
  j += "}";
  sendJson(j);
}

static void apiProbe() {
  if (!guard()) return;
  uint16_t ms = 0;
  const int code = netProbeProxy(&ms);
  sendJson("{\"code\":" + String(code) + ",\"ms\":" + String(ms) + "}");
}

/**
 * Relay a catalog read through the device, for when the browser can reach the
 * panel but not the proxy — a phone on a guest VLAN, say.
 *
 * PATH allowlist, never a URL: accepting a host from the client would make
 * this an SSRF primitive pointed at the user's own LAN.
 */
static void apiRelay() {
  if (!guard()) return;
  const String p = s_srv.arg("p");
  const bool ok = p == "/v1/catalog" || p == "/v1/catalog?teams=1" ||
                  p == "/v1/health" ||
                  (p.startsWith("/v1/teams/") && p.length() <= 20);
  if (!ok) return fail(400, "path not allowed");
  if (!g_set.proxy.length()) return fail(503, "no proxy configured");
  // Scores outrank setup: never compete with a poll for the TLS buffer.
  if (g_pollInFlight || !netGateOpen()) return fail(503, "busy - try again in a moment");

  String body;
  const int code = netRelayGet(p, body);
  if (code != 200) return fail(502, "proxy did not answer");
  noStore();
  s_srv.send(200, "application/json", body);
}

static void apiWifi() {
  if (!guard()) return;
  const String body = bodyOf();
  String ssid, pass;
  if (!jsonField(body, "ssid", ssid) || !ssid.length()) return fail(400, "network name required");
  jsonField(body, "pass", pass);
  g_set.ssid = ssid;
  applySecret(g_set.pass, pass);
  settingsSave();
  sendJson("{\"ok\":true}");
  delay(250);
  ESP.restart();
}

static void apiReboot() { if (!guard()) return; sendJson("{\"ok\":true}"); delay(250); ESP.restart(); }

static void apiForget() {
  if (!guard()) return;
  g_set.ssid = ""; g_set.pass = "";
  settingsSave();
  sendJson("{\"ok\":true}");
  delay(250);
  ESP.restart();
}

static void apiReset() {
  if (!guard()) return;
  settingsFactoryReset();
  sendJson("{\"ok\":true}");
  delay(250);
  ESP.restart();
}

// ── OTA ────────────────────────────────────────────────────────────────────

static void pageUpdate() {
  s_srv.sendHeader("Connection", "close");
  if (s_otaAuthFailed) {
    s_otaAuthFailed = false;
    s_srv.send(403, "text/plain", "not authorised");
    return;
  }
  const bool ok = !Update.hasError();
  // Report the real reason. The old code discarded errorString() and said
  // "FAILED", which tells the user nothing they can act on.
  s_srv.send(ok ? 200 : 500, "text/plain", ok ? "OK, restarting" : Update.errorString());
  delay(400);
  if (ok) ESP.restart();
}

static void pageUpload() {
  HTTPUpload& up = s_srv.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_otaAuthFailed = false;
    if (!hostAllowed() || !originAllowed() ||
        (g_set.panelPass.length() && !s_srv.authenticate("admin", g_set.panelPass.c_str()))) {
      // Flag AND abort. Simply returning let the whole unauthorised upload
      // stream to completion before anything reported a problem.
      s_otaAuthFailed = true;
      Update.abort();
      return;
    }
    Serial.printf("[web] OTA start: %s\n", up.filename.c_str());
    // The panel will shake for the duration — every flash write stalls the DMA
    // 150-220 ms. Saying so is the difference between "working" and "broken".
    uiToast("UPDATING");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      Serial.printf("[web] OTA begin failed: %s\n", Update.errorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_otaAuthFailed) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize)
      Serial.printf("[web] OTA write failed: %s\n", Update.errorString());
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_otaAuthFailed) return;
    if (Update.end(true)) Serial.printf("[web] OTA done: %u bytes\n", up.totalSize);
    else Serial.printf("[web] OTA end failed: %s\n", Update.errorString());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    // Without this a cancelled upload leaves the OTA partition half written
    // and the next attempt fails for a reason that looks unrelated.
    Update.abort();
    Serial.println("[web] OTA aborted");
  }
}

// ── lifecycle ──────────────────────────────────────────────────────────────

void webBegin() {
  if (s_up) return;
  if (MDNS.begin(mdnsName().c_str())) MDNS.addService("http", "tcp", 80);
  const char* wanted[] = { "Origin" };
  s_srv.collectHeaders(wanted, 1);

  s_srv.on("/", HTTP_GET, pageRoot);
  s_srv.on("/api/config", HTTP_GET, apiConfigGet);
  s_srv.on("/api/config", HTTP_POST, apiConfigPost);
  s_srv.on("/api/favs", HTTP_POST, apiFavsPost);
  s_srv.on("/api/state", HTTP_GET, apiState);
  s_srv.on("/api/diag", HTTP_GET, apiDiag);
  s_srv.on("/api/probe", HTTP_GET, apiProbe);
  s_srv.on("/api/relay", HTTP_GET, apiRelay);
  s_srv.on("/api/wifi", HTTP_POST, apiWifi);
  s_srv.on("/api/reboot", HTTP_POST, apiReboot);
  s_srv.on("/api/forget", HTTP_POST, apiForget);
  s_srv.on("/api/reset", HTTP_POST, apiReset);
  s_srv.on("/update", HTTP_POST, pageUpdate, pageUpload);
  s_srv.onNotFound([] { s_srv.send(404, "text/plain", "not found"); });
  s_srv.begin();
  s_up = true;

  Serial.printf("[web] portal on http://%s/ (also http://%s.local/)\n",
                WiFi.localIP().toString().c_str(), mdnsName().c_str());
  if (!g_set.panelPass.length())
    Serial.println("[web] WARNING: no portal password — anyone on this LAN can reflash this panel");
}

void webLoop() { if (s_up) s_srv.handleClient(); }
bool webUp() { return s_up; }
