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
  // `forTxtReader` drops EPUB-only settings (Images, Embedded Style) — TXT
  // files have no embedded images and no HTML/CSS styling, so those toggles
  // are meaningless in TXT context.
  explicit ReaderOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool forTxtReader = false);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  // Build the filtered list. Reader Options exposes:
  //   - Font selection (action)
  //   - All STR_CAT_READER entries EXCEPT STR_ORIENTATION (the reader menu
  //     already cycles orientation inline — duplicate exposure is confusing)
  //   - All STR_CAT_CONTROLS entries
  //   - STR_SUNLIGHT_FADING_FIX (display category, last)
  // STR_IMAGES and STR_EMBEDDED_STYLE are also dropped when forTxtReader is
  // true. Only TOGGLE/ENUM/VALUE types are kept here; ACTION items are added
  // explicitly by buildSettings().
  static std::vector<SettingInfo> buildSettings(bool forTxtReader);

  void toggleCurrentSetting();

  ButtonNavigator buttonNavigator;
  std::vector<SettingInfo> settings;
  bool forTxtReader = false;
  int selectedSettingIndex = 0;
  int settingsCount = 0;
};
