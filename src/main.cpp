#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <SPI.h>
#include <SdFontFamily.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/home/MyLibraryActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

HalDisplay display;
HalGPIO gpio;
MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
Activity* currentActivity;

// UI Font (Pretendard 10pt) - Regular only, synthetic bold applied by renderer
EpdFont pretendard10RegularFont(&pretendard_10_regular);
EpdFontFamily uiFontFamily(&pretendard10RegularFont);

// Korean EPUB reader font (KoPub Batang 14pt) - Regular only, synthetic bold applied by renderer
EpdFont kopub14RegularFont(&kopub_14_regular);
EpdFontFamily kopub14FontFamily(&kopub14RegularFont);

// Korean fonts loading from SD card is disabled due to memory constraints
// Font files should be in /.crosspoint/fonts/ directory
constexpr char FONT_DIR[] = "/.crosspoint/fonts";

// Helper function to safely load an SD font with comprehensive error handling
// Returns true if loading succeeded
bool trySdFontLoad(GfxRenderer& renderer, int fontId, const char* name, const char* regularPath,
                   const char* boldPath = nullptr) {
  // First check if the file exists before attempting to create SdFontFamily
  if (!Storage.exists(regularPath)) {
    Serial.printf("[%lu] [FNT] %s not found: %s\n", millis(), name, regularPath);
    return false;
  }

  SdFontFamily* font = nullptr;
  bool success = false;

  // Create font family - use regular new since ESP32 doesn't always support nothrow
  font = new SdFontFamily(regularPath, boldPath);
  if (font == nullptr) {
    Serial.printf("[%lu] [FNT] Failed to allocate memory for %s\n", millis(), name);
    return false;
  }

  if (font->load()) {
    renderer.insertSdFont(fontId, font);
    Serial.printf("[%lu] [FNT] Loaded %s from SD\n", millis(), name);
    success = true;
  } else {
    Serial.printf("[%lu] [FNT] Failed to load %s\n", millis(), name);
    delete font;
  }

  return success;
}

// Track which SD fonts were successfully loaded
static bool sdFontsLoaded[6] = {false, false, false, false, false, false};
enum SdFontIndex {
  SD_PRETENDARD_10 = 0,
  SD_PRETENDARD_12 = 1,
  SD_EULYOO_12 = 2,
  SD_EULYOO_14 = 3,
  SD_EULYOO_16 = 4,
  SD_EULYOO_18 = 5
};

// Load custom reader font from SD card if configured
// Returns true if custom font was loaded successfully
bool loadCustomReaderFont(GfxRenderer& gfxRenderer) {
  if (!SETTINGS.hasCustomFont()) {
    Serial.printf("[%lu] [FNT] No custom font configured, using default KoPub Batang\n", millis());
    return false;
  }

  const char* fontPath = SETTINGS.customFontPath;
  Serial.printf("[%lu] [FNT] Loading custom font: %s\n", millis(), fontPath);

  if (!Storage.exists(fontPath)) {
    Serial.printf("[%lu] [FNT] Custom font file not found: %s\n", millis(), fontPath);
    // Clear invalid font path
    SETTINGS.customFontPath[0] = '\0';
    SETTINGS.saveToFile();
    return false;
  }

  // Try to load the custom font
  if (trySdFontLoad(gfxRenderer, CUSTOM_FONT_ID, "CustomReaderFont", fontPath)) {
    Serial.printf("[%lu] [FNT] Custom reader font loaded successfully\n", millis());
    return true;
  }

  Serial.printf("[%lu] [FNT] Failed to load custom font, clearing setting to use default\n", millis());
  // Clear invalid font path so getReaderFontId() returns default font
  SETTINGS.customFontPath[0] = '\0';
  SETTINGS.saveToFile();
  return false;
}

// Reload custom reader font - removes old font and loads new one
// Call this when font settings change to apply immediately without reboot
bool reloadCustomReaderFont() {
  Serial.printf("[%lu] [FNT] Reloading custom reader font...\n", millis());

  // Remove existing custom font if any
  if (renderer.hasFont(CUSTOM_FONT_ID)) {
    renderer.removeFont(CUSTOM_FONT_ID);
    Serial.printf("[%lu] [FNT] Removed previous custom font\n", millis());
  }

  // Load new custom font if configured
  return loadCustomReaderFont(renderer);
}

// Get reference to global renderer (for font operations from other modules)
GfxRenderer& getGlobalRenderer() { return renderer; }

// SD font loading is disabled - Korean fonts need to be embedded in flash
// due to ESP32-C3 memory constraints. SD card loading causes crashes.
void loadSdFonts(GfxRenderer& /*renderer*/) {
  // SD font loading disabled - use flash-embedded fonts instead
  Serial.printf("[%lu] [FNT] SD font loading disabled (use flash fonts)\n", millis());
}

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  currentActivity = activity;
  currentActivity->onEnter();
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    gpio.startDeepSleep();
  }
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  APP_STATE.lastSleepFromReader = currentActivity && currentActivity->isReaderActivity();
  APP_STATE.saveToFile();
  exitActivity();
  enterNewActivity(new SleepActivity(renderer, mappedInputManager));

  display.deepSleep();
  Serial.printf("[%lu] [   ] Power button press calibration value: %lu ms\n", millis(), t2 - t1);
  Serial.printf("[%lu] [   ] Entering deep sleep.\n", millis());

  gpio.startDeepSleep();
}

void onGoHome();
void onGoToMyLibraryWithPath(const std::string& path);
void onGoToRecentBooks();
void onGoToReader(const std::string& initialEpubPath) {
  exitActivity();
  enterNewActivity(
      new ReaderActivity(renderer, mappedInputManager, initialEpubPath, onGoHome, onGoToMyLibraryWithPath));
}

void onGoToFileTransfer() {
  exitActivity();
  enterNewActivity(new CrossPointWebServerActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToSettings() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToMyLibrary() {
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader));
}

void onGoToRecentBooks() {
  exitActivity();
  enterNewActivity(new RecentBooksActivity(renderer, mappedInputManager, onGoHome, onGoToReader));
}

void onGoToMyLibraryWithPath(const std::string& path) {
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader, path));
}

void onGoToBrowser() {
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(renderer, mappedInputManager, onGoHome));
}

void onGoHome() {
  exitActivity();
  enterNewActivity(new HomeActivity(renderer, mappedInputManager, onGoToReader, onGoToMyLibrary, onGoToRecentBooks,
                                    onGoToSettings, onGoToFileTransfer, onGoToBrowser));
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  Serial.printf("[%lu] [   ] Display initialized\n", millis());

  // UI font (Pretendard 10pt)
  renderer.insertFont(UI_FONT_ID, &uiFontFamily);
  renderer.insertFont(UI_10_FONT_ID, &uiFontFamily);
  renderer.insertFont(UI_12_FONT_ID, &uiFontFamily);
  renderer.insertFont(SMALL_FONT_ID, &uiFontFamily);

  // Korean EPUB reader font (KoPub Batang 14pt) - always register as fallback
  renderer.insertFont(KOPUB_14_FONT_ID, &kopub14FontFamily);

  // Try to load custom reader font from SD card
  loadCustomReaderFont(renderer);

  // Set fallback font to Pretendard UI
  renderer.setFallbackFont(UI_FONT_ID);

  // SD card fonts loading disabled due to memory constraints
  loadSdFonts(renderer);

  Serial.printf("[%lu] [   ] Fonts setup complete\n", millis());
}

void setup() {
  t1 = millis();

  gpio.begin();

  // Only start serial if USB connected
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    // Wait up to 3 seconds for Serial to be ready to catch early logs
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
      delay(10);
    }
  }

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    Serial.printf("[%lu] [   ] SD card initialization failed\n", millis());
    setupDisplayAndFonts();
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInputManager, "SD card error", EpdFontFamily::BOLD));
    return;
  }

  SETTINGS.loadFromFile();
  KOREADER_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      // For normal wakeups, verify power button press duration
      Serial.printf("[%lu] [   ] Verifying power button press duration\n", millis());
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      Serial.printf("[%lu] [   ] Wakeup reason: After USB Power\n", millis());
      gpio.startDeepSleep();
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  Serial.printf("[%lu] [   ] Starting CrossPoint version " CROSSPOINT_VERSION "\n", millis());

  setupDisplayAndFonts();

  exitActivity();
  enterNewActivity(new BootActivity(renderer, mappedInputManager));

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
  // crashed (indicated by readerActivityLoadCount > 0)
  if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
      mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    onGoHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    onGoToReader(path);
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    Serial.printf("[%lu] [MEM] Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();  // Reset inactivity timer
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    Serial.printf("[%lu] [SLP] Auto-sleep triggered after %lu ms of inactivity\n", millis(), sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  const unsigned long activityStartTime = millis();
  if (currentActivity) {
    currentActivity->loop();
  }
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      Serial.printf("[%lu] [LOOP] New max loop duration: %lu ms (activity: %lu ms)\n", millis(), maxLoopDuration,
                    activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();  // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    delay(10);  // Normal delay when no activity requires fast response
  }
}
