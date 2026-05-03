#pragma once

#include <set>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Browse and toggle which BMPs in /.sleep and /sleep are eligible for the
// random Custom sleep-screen rotation. Persists via SleepImageSelectionStore.
//
// UX:
//   - Up / Down / Left / Right : navigate previous / next image
//   - Confirm                  : toggle the current image's selected state
//   - Back                     : save the store and exit
//
// Each image is rendered full-screen using the same path as SleepActivity
// (Bitmap + GfxRenderer), with a small overlay strip for filename and the
// selected indicator so what the user sees here is what they will see at
// sleep time.
class SleepImageSelectionActivity final : public Activity {
 public:
  explicit SleepImageSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SleepImageSelection", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Walks /.sleep and /sleep, collects any readable BMP, and runs the store's
  // prune pass against the discovered set so deleted files drop out of the
  // saved selection.
  void scanDirs();

  std::vector<std::string> imagePaths;  // full paths, e.g. "/.sleep/foo.bmp"
  int currentIndex = 0;
  bool dirty = false;  // any toggle since onEnter
  ButtonNavigator buttonNavigator;
};
