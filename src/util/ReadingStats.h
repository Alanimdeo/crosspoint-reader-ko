#pragma once

#include <cstdint>
#include <string>

// Per-book reading statistics persisted to <cachePath>/reading_stats.bin.
// Layout (little-endian, 16 bytes total):
//   [0..3]   magic 'TIME' (0x454D4954 LE)
//   [4]      version = 1
//   [5..7]   reserved (zero)
//   [8..11]  totalSeconds  uint32  — cumulative reading time in seconds
//   [12..15] sessionCount  uint32  — number of distinct sessions saved
//
// uint32 seconds covers ~136 years of reading; far beyond device lifetime.
// We store seconds (not ms) to dodge millis()'s 49.7-day rollover cleanly
// and to keep the file tiny.
class ReadingStats {
 public:
  static constexpr uint32_t MAGIC = 0x454D4954u;  // 'TIME' LE
  static constexpr uint8_t FILE_VERSION = 1;
  static constexpr size_t FILE_SIZE = 16;

  uint32_t totalSeconds = 0;
  uint32_t sessionCount = 0;

  // Load from <cachePath>/reading_stats.bin. Returns false if the file is
  // missing or malformed; in that case `out` is left zeroed.
  static bool load(const std::string& cachePath, ReadingStats& out);

  // Persist to <cachePath>/reading_stats.bin. The cache directory must
  // already exist (callers invoke setupCacheDir() during onEnter).
  static bool save(const std::string& cachePath, const ReadingStats& stats);

  // Format `seconds` into a localized-ish short string ("45m", "1h 23m",
  // "1d 5h", "123d") and write to `buf`. Always null-terminates.
  // bufSize must be >= 12 to hold the longest 100+ day case safely.
  static void format(uint32_t seconds, char* buf, size_t bufSize);
};
