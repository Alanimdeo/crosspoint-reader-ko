#pragma once

#include <Memory.h>

#include <string>

#include "LuaRuntime.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"

// Runs a single .lua script from the SD card. The script is loaded and executed
// once in onEnter(); Back exits back to the file browser. The Lua VM is created
// here and destroyed in onExit(), so all VM memory is returned to the heap when
// the mini-app closes.
class LuaRunnerActivity final : public Activity {
 public:
  LuaRunnerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string scriptPath,
                    int scriptVersion = 0);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // While a script is running the loop must keep cycling so button events are
  // forwarded to the VM hook on time.
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return true; }

 private:
  void dispatchInputEvents();
  void invokeKeyboardHandler(const char* edge, const char* key);
  void invokeSwipeHandler(const char* dir);
  bool hasInputHandler(const char* edge, const char* key);
  void checkFrameDue();
  void invokeFrameHandlerOnly();

  std::string scriptPath;
  int scriptVersion;
  std::unique_ptr<LuaRuntime> lua;
  std::string startError;
  bool scriptStarted = false;
  bool rerunRequested = false;
};