#pragma once
#include <functional>
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "fontIds.h"

class ConfirmationActivity : public Activity {
 private:
  // Popup title, built from the heading in onEnter() (truncated to fit the popup).
  std::string popupTitle;

  OptionPopup confirmPopup;

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};