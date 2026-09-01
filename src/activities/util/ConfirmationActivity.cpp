#include "ConfirmationActivity.h"

#include <I18n.h>

#include "HalDisplay.h"
#include "components/UITheme.h"

namespace {
constexpr int kMargin = 20;
constexpr int kFontId = UI_10_FONT_ID;
}  // namespace

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : Activity("Confirmation", renderer, mappedInput), body(body) {
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
  if (!body.empty()) {
    // Keep the body line short so it fits the popup (same truncation rule as
    // the title, regular style).
    confirmPopup.setBody(renderer.truncatedText(kFontId, body.c_str(), renderer.getScreenWidth() - (kMargin * 2),
                                                EpdFontFamily::REGULAR));
  }

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  // No clearScreen: the previous activity's frame is still in the framebuffer
  // (only one buffer shared with the underlying screen), so the popup overlays
  // the last displayed screen instead of forcing a white background.

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
