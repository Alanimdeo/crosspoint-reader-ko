#include "ActivityWithSubactivity.h"

#include "ActivityManager.h"

// Exit the current sub-activity and restore this activity as current
void ActivityWithSubactivity::exitActivity() { subActivity = nullptr; }

// Enter a new sub-activity (takes ownership)
void ActivityWithSubactivity::enterNewActivity(Activity* activity) {
  subActivity.reset(activity);
  subActivity->onEnter();
}

// Convenience: alias for enterNewActivity (used by callers that follow exit+enter pattern)
void ActivityWithSubactivity::enterSubActivity(Activity* activity) { enterNewActivity(activity); }

void ActivityWithSubactivity::loop() {
  if (subActivity) {
    subActivity->loop();
  }
}

void ActivityWithSubactivity::requestUpdate(bool immediate) {
  if (!subActivity) {
    Activity::requestUpdate(immediate);
  }
  // If a sub-activity is active, ignore parent requestUpdate — sub-activity drives its own renders.
}

void ActivityWithSubactivity::onExit() {
  if (subActivity) {
    subActivity->onExit();
    subActivity = nullptr;
  }
  Activity::onExit();
}
