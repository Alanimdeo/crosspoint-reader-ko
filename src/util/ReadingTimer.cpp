#include "ReadingTimer.h"

#include <Logging.h>

namespace {
constexpr const char* MODULE = "RTIM";
}

void ReadingTimer::start(const std::string& cachePath) {
  _cachePath = cachePath;
  _stats = ReadingStats{};
  ReadingStats::load(_cachePath, _stats);

  const uint32_t now = millis();
  _lastTickMs = now;
  _lastInputMs = now;
  _lastSaveMs = now;
  _pendingMs = 0;
  _sessionSeconds = 0;
  _dirty = false;
  _active = true;
  LOG_DBG(MODULE, "Started — total=%lus", static_cast<unsigned long>(_stats.totalSeconds));
}

void ReadingTimer::stop() {
  if (!_active) return;
  // Drain any sub-second residue so a session that just barely crosses a
  // whole-second boundary still counts.
  tick();

  if (_sessionSeconds >= MIN_SESSION_SAVE_SEC) {
    _stats.sessionCount += 1;
    _dirty = true;
  }
  if (_dirty) {
    ReadingStats::save(_cachePath, _stats);
  }
  LOG_DBG(MODULE, "Stopped — session=%lus total=%lus", static_cast<unsigned long>(_sessionSeconds),
          static_cast<unsigned long>(_stats.totalSeconds));
  _active = false;
}

void ReadingTimer::tick() {
  if (!_active) return;

  const uint32_t now = millis();
  uint32_t delta = static_cast<uint32_t>(now - _lastTickMs);
  _lastTickMs = now;

  // Sub-activity returns, sleeps, or unrelated stalls show up as huge deltas.
  // Drop them so menu time isn't credited as reading time.
  if (delta > MAX_DELTA_MS) {
    delta = 0;
  }

  // Idle pause — no input for IDLE_TIMEOUT_MS suspends accumulation.
  if (static_cast<uint32_t>(now - _lastInputMs) >= IDLE_TIMEOUT_MS) {
    return;
  }

  if (delta == 0) return;

  _pendingMs += delta;
  if (_pendingMs >= 1000) {
    const uint32_t addedSec = _pendingMs / 1000;
    _pendingMs -= addedSec * 1000;
    _stats.totalSeconds += addedSec;
    _sessionSeconds += addedSec;
    _dirty = true;
  }

  if (_dirty && static_cast<uint32_t>(now - _lastSaveMs) >= MIDSAVE_INTERVAL_MS &&
      _sessionSeconds >= MIN_SESSION_SAVE_SEC) {
    persistIfDirty();
    _lastSaveMs = now;
  }
}

void ReadingTimer::notifyInput() {
  if (!_active) return;
  _lastInputMs = millis();
}

void ReadingTimer::reset() {
  _stats.totalSeconds = 0;
  _stats.sessionCount = 0;
  _sessionSeconds = 0;
  _pendingMs = 0;
  _dirty = false;
  if (_active) {
    ReadingStats::save(_cachePath, _stats);
    _lastSaveMs = millis();
  }
  LOG_INF(MODULE, "Reset by user");
}

void ReadingTimer::persistIfDirty() {
  if (!_dirty) return;
  if (ReadingStats::save(_cachePath, _stats)) {
    _dirty = false;
  }
}
