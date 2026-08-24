#pragma once
// =============================================================================
//  settings-core — shared NVS settings plumbing
//
//  Every app persists a small `Settings` struct in NVS flash, seeded from its
//  config.h defaults on first boot and edited live from the web page. The field
//  sets differ per app, so each keeps its own struct — but the Preferences
//  open/get/put/end dance (and the brightness clamp everyone needs) live here.
//
//  PrefsStore is a thin RAII veneer over Arduino's Preferences: construct it for
//  a namespace, load in readonly mode, save in read-write mode. NVS keys are
//  passed through unchanged, so existing saved settings keep loading.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>

// Clamp a (possibly out-of-range) brightness to [lo, hi].
static inline uint8_t clampBrightness(int v, uint8_t lo, uint8_t hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return (uint8_t)v;
}

class PrefsStore {
 public:
  explicit PrefsStore(const char* ns) : ns_(ns) {}

  // Open for reading (defaults returned for missing keys) or writing.
  void beginRead()  { prefs_.begin(ns_, /*readOnly=*/true); }
  void beginWrite() { prefs_.begin(ns_, /*readOnly=*/false); }
  void end()        { prefs_.end(); }

  // Typed getters (return `def` when the key is absent).
  uint8_t  u8(const char* k, uint8_t def)   { return prefs_.getUChar(k, def); }
  uint16_t u16(const char* k, uint16_t def) { return prefs_.getUShort(k, def); }
  uint32_t u32(const char* k, uint32_t def) { return prefs_.getUInt(k, def); }
  int      i32(const char* k, int def)      { return prefs_.getInt(k, def); }
  bool     boolean(const char* k, bool def) { return prefs_.getBool(k, def); }
  String   str(const char* k, const char* def) { return prefs_.getString(k, def); }
  // Copy a stored string into a fixed buffer (uses `def` if absent).
  void strTo(const char* k, const char* def, char* out, size_t outSize) {
    strlcpy(out, prefs_.getString(k, def).c_str(), outSize);
  }

  // Typed setters.
  void put(const char* k, uint8_t v)  { prefs_.putUChar(k, v); }
  void put(const char* k, uint16_t v) { prefs_.putUShort(k, v); }
  void put(const char* k, uint32_t v) { prefs_.putUInt(k, v); }
  void put(const char* k, int v)      { prefs_.putInt(k, v); }
  void put(const char* k, bool v)     { prefs_.putBool(k, v); }
  void put(const char* k, const char* v) { prefs_.putString(k, v); }

  Preferences& raw() { return prefs_; }   // escape hatch for anything unusual

 private:
  const char* ns_;
  Preferences prefs_;
};
