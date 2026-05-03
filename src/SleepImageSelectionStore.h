#pragma once

#include <set>
#include <string>

// Persists which custom sleep-screen BMPs (under /.sleep and /sleep on the SD
// card) the user has selected for random rotation. Stored on the SD card
// alongside the images themselves so a card swap moves the selection with
// the images, and so an OTA / SPIFFS reset doesn't wipe it.
//
// File format: plain text, one absolute path per line (e.g. "/.sleep/foo.bmp").
// Selection rules consumed by SleepActivity:
//   - empty store (no file or no entries)  -> use ALL discovered BMPs
//   - non-empty                            -> use only the selected entries
//   - non-empty but every entry is missing -> fail-safe back to ALL
class SleepImageSelectionStore {
 public:
  static SleepImageSelectionStore& getInstance() {
    static SleepImageSelectionStore instance;
    return instance;
  }

  void loadFromFile();
  bool saveToFile() const;

  bool isSelected(const std::string& fullPath) const { return selected.count(fullPath) > 0; }
  void setSelected(const std::string& fullPath, bool isSelected);
  bool empty() const { return selected.empty(); }
  const std::set<std::string>& getSelected() const { return selected; }

  // Drop any entries whose path is not in `existingPaths`. Returns true if
  // anything was pruned (caller can decide whether to flush to disk).
  bool pruneMissing(const std::set<std::string>& existingPaths);

 private:
  SleepImageSelectionStore() = default;
  std::set<std::string> selected;
  bool loaded = false;
};

#define SLEEP_IMAGE_SELECTION SleepImageSelectionStore::getInstance()
