// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Franco Raso
// Preferences.h — desktop shim. NVS becomes an in-memory map, so the harness
// can exercise settingsLoad()/settingsSave*() for real without a flash chip.
// Nothing persists between runs, which is what you want when experimenting.
#pragma once
#include <map>
#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool   getBool  (const char* k, bool d = false)        { auto i = b.find(k); return i == b.end() ? d : i->second; }
  int    getInt   (const char* k, int d = 0)             { auto i = n.find(k); return i == n.end() ? d : (int)i->second; }
  double getDouble(const char* k, double d = 0)          { auto i = f.find(k); return i == f.end() ? d : i->second; }
  String getString(const char* k, const char* d = "")    { auto i = s.find(k); return i == s.end() ? String(d) : i->second; }
  size_t putBool  (const char* k, bool v)   { b[k] = v; return 1; }
  size_t putInt   (const char* k, int v)    { n[k] = v; return 4; }
  size_t putDouble(const char* k, double v) { f[k] = v; return 8; }
  size_t putString(const char* k, const String& v) { s[k] = v; return v.length(); }

  // ScoreDeck stores density, quiet-hour minutes and the alert sequence as
  // fixed-width integers. They share the integer map — the widths only matter
  // to NVS, and the harness has no NVS.
  // Factory reset. The harness has no NVS, so this just empties the maps —
  // enough for the settings screen and the portal to be exercised.
  bool clear() { s.clear(); n.clear(); return true; }
  bool remove(const char* k) { s.erase(k); n.erase(k); return true; }

  uint8_t  getUChar (const char* k, uint8_t d = 0)  { auto i = n.find(k); return i == n.end() ? d : (uint8_t)i->second; }
  uint16_t getUShort(const char* k, uint16_t d = 0) { auto i = n.find(k); return i == n.end() ? d : (uint16_t)i->second; }
  uint32_t getULong (const char* k, uint32_t d = 0) { auto i = n.find(k); return i == n.end() ? d : (uint32_t)i->second; }
  size_t   putUChar (const char* k, uint8_t v)      { n[k] = v; return 1; }
  size_t   putUShort(const char* k, uint16_t v)     { n[k] = v; return 2; }
  size_t   putULong (const char* k, uint32_t v)     { n[k] = v; return 4; }
 private:
  std::map<std::string, bool>        b;
  std::map<std::string, long>        n;
  std::map<std::string, double>      f;
  std::map<std::string, String>      s;
};
