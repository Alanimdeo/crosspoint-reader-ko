#pragma once

#include <Arduino.h>

#include <string>

#include "ReadingStats.h"

// Per-book reading-time accumulator embedded in reader activities.
//
// Lifecycle:
//   start()     — call from onEnter() after the book's cache dir is ready.
//   tick()      — call from loop() every iteration; accumulates elapsed seconds.
//   notifyInput() — call whenever the user produces input or an automatic
//                   page turn fires; resets the idle-pause timer.
//   stop()      — call from onExit(); finalises the session and persists.
//   reset()     — clear the accumulated total (menu action).
//
// Time source: millis(). All deltas use uint32_t subtraction so they stay
// correct across the 49.7-day rollover. Per-tick deltas are clamped to
// MAX_DELTA_MS to absorb large gaps from sub-activities or sleep without
// crediting them as reading time.
class ReadingTimer {
 public:
  // After this many ms with no input, accumulation pauses until the next
  // notifyInput() call. Five minutes matches the proposal default.
  static constexpr uint32_t IDLE_TIMEOUT_MS = 5UL * 60UL * 1000UL;
  // Mid-session save cadence; bounded by SPIFFS-style wear concerns even
  // though we write to SD here, to keep heap pressure / SD churn low.
  static constexpr uint32_t MIDSAVE_INTERVAL_MS = 5UL * 60UL * 1000UL;
  // A single tick covering more than this is treated as time-not-spent-reading
  // (sub-activity, sleep, USB enumeration stall, etc.).
  static constexpr uint32_t MAX_DELTA_MS = 2000UL;
  // Sessions shorter than this don't bump sessionCount or trigger a save —
  // avoids SD writes when the user just glances at a book.
  static constexpr uint32_t MIN_SESSION_SAVE_SEC = 30UL;

  void start(const std::string& cachePath);
  void stop();
  void tick();
  void notifyInput();
  void reset();

  uint32_t totalSeconds() const { return _stats.totalSeconds; }
  uint32_t sessionSeconds() const { return _sessionSeconds; }

 private:
  void persistIfDirty();

  std::string _cachePath;
  ReadingStats _stats{};

  bool _active = false;
  bool _dirty = false;
  uint32_t _lastTickMs = 0;
  uint32_t _lastInputMs = 0;
  uint32_t _lastSaveMs = 0;
  uint32_t _pendingMs = 0;       // sub-second carry, < 1000
  uint32_t _sessionSeconds = 0;  // current session only
};
