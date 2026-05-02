#pragma once
#include <I18n.h>

#include <vector>

#include "SettingsActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Reader-side settings flyout reachable from the EPUB/TXT reader menu.
//
// The setting definitions are SHARED with the home settings screen via the
// global `getSettingsList()` registry — this activity simply filters that
// registry by category/key whitelist and reuses the same toggle semantics.
// New settings added to `SettingsList.h` automatically surface here as long
// as they belong to the included categories.
class ReaderOptionsActivity final : public Activity {
 public:
  explicit ReaderOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  // Build the filtered list. Whitelist:
  //   - Display: STR_HIDE_BATTERY, STR_REFRESH_FREQ, STR_SUNLIGHT_FADING_FIX
  //   - All STR_CAT_READER entries
  //   - All STR_CAT_CONTROLS entries
  // Only TOGGLE/ENUM/VALUE types are kept; ACTION/STRING types are skipped.
  static std::vector<SettingInfo> buildSettings();

  void toggleCurrentSetting();

  ButtonNavigator buttonNavigator;
  std::vector<SettingInfo> settings;
  int selectedSettingIndex = 0;
  int settingsCount = 0;
};
