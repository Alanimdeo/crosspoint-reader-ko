#include "ConfirmationActivity.h"

#include <I18n.h>

#include "HalDisplay.h"
#include "components/UITheme.h"

namespace {
constexpr int kMargin = 20;
constexpr int kFontId = UI_10_FONT_ID;
}  // namespace

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading)
    : Activity("Confirmation", renderer, mappedInput) {
  // Truncate the heading in the constructor (renderer width is available here);
  // the popup carries it as its title and Cancel/Confirm as options.
  const int maxWidth = renderer.getScreenWidth() - (kMargin * 2);
  popupTitle =
      heading.empty() ? std::string() : renderer.truncatedText(kFontId, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  const char* options[] = {I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM)};
  confirmPopup.show(popupTitle.c_str(), options, 2, 0, [this](int idx) {
    ActivityResult res;
    res.isCancelled = (idx != 1);
    setResult(std::move(res));
    finish();
  });

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Popup dismissed without a selection (Back button or tap outside): cancel.
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
