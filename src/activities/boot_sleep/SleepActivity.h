#pragma once
#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false,
                         bool quietRepaint = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout), quietRepaint(quietRepaint) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  void renderClockSleepScreen() const;

  bool fromTimeout = false;
  // Timer-wake clock re-render: skip the "Entering sleep" popup and any
  // full-screen flash so the periodic re-paint is a single cheap refresh.
  bool quietRepaint = false;
};
