#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SleepImageSelectionStore.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  // Show popup with reader orientation only when going to sleep from reader.
  // Timer-wake clock re-renders (quietRepaint) skip the popup entirely — the
  // whole point is a single cheap refresh with no flash or message.
  if (!quietRepaint) {
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    } else {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CLOCK):
      return renderClockSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Check if we have a /.sleep (preferred) or /sleep directory
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      if (dir) dir.close();
      return;
    }
    file.close();
  }

  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (sleepDir) {
    std::vector<std::string> files;
    char name[500];
    // collect all valid BMP files
    for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
      if (dirFile.isDirectory()) {
        dirFile.close();
        continue;
      }
      dirFile.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        dirFile.close();
        continue;
      }

      if (!FsHelpers::hasBmpExtension(filename)) {
        LOG_DBG("SLP", "Skipping non-.bmp file name: %s", name);
        dirFile.close();
        continue;
      }
      Bitmap bitmap(dirFile);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SLP", "Skipping invalid BMP file: %s", name);
        dirFile.close();
        continue;
      }
      files.emplace_back(filename);
      dirFile.close();
    }
    // Honor the user's per-file selection from Sleep Image Selection. The
    // store keeps absolute paths ("/.sleep/foo.bmp"), so we filter `files`
    // (which are bare filenames at this point) by reconstructing the same
    // form. Fail-safe rules:
    //   - empty store     -> use ALL discovered BMPs (backward compat)
    //   - non-empty store -> keep only matching entries
    //   - filtered empty  -> fall back to ALL (the user shouldn't end up on
    //                        the default sleep screen because every saved
    //                        path is missing — better to keep showing the
    //                        candidates that DO exist).
    SleepImageSelectionStore::getInstance().loadFromFile();
    if (!SleepImageSelectionStore::getInstance().empty()) {
      std::vector<std::string> filtered;
      filtered.reserve(files.size());
      for (const auto& fname : files) {
        const std::string fullPath = std::string(sleepDir) + "/" + fname;
        if (SleepImageSelectionStore::getInstance().isSelected(fullPath)) {
          filtered.push_back(fname);
        }
      }
      if (!filtered.empty()) {
        files = std::move(filtered);
      }
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Pick a random wallpaper, excluding recently shown ones.
      // Window: up to SLEEP_RECENT_COUNT entries, capped at numFiles-1.
      const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
      const uint8_t window =
          static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
      auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
      for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(randomFileIndex, window); attempt++) {
        randomFileIndex = static_cast<uint16_t>(random(fileCount));
      }
      APP_STATE.pushRecentSleep(randomFileIndex);
      APP_STATE.saveToFile();
      const auto filename = std::string(sleepDir) + "/" + files[randomFileIndex];
      HalFile randFile;
      if (Storage.openFileForRead("SLP", filename, randFile)) {
        LOG_DBG("SLP", "Randomly loading: %s/%s", sleepDir, files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(randFile, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          randFile.close();
          dir.close();
          return;
        }
        randFile.close();
      }
    }
  }
  if (dir) dir.close();

  renderDefaultSleepScreen();
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    // Greyscale rendering reads the BMP file 2 more times. If rewind fails,
    // skip greyscale to preserve the BW image already displayed above.
    if (bitmap.rewindToData() != BmpReaderError::Ok) {
      LOG_ERR("SLP", "Failed to rewind for greyscale LSB, skipping greyscale");
      return;
    }
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    if (bitmap.rewindToData() != BmpReaderError::Ok) {
      LOG_ERR("SLP", "Failed to rewind for greyscale MSB, skipping greyscale");
      renderer.setRenderMode(GfxRenderer::BW);
      return;
    }
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    // Don't use fadingFix for sleep screen: it powers off the display before
    // greyscale LUT fully settles, causing white screen. deepSleep() handles
    // the proper power-down sequence afterwards.
    renderer.setFadingFix(false);
    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    LOG_DBG("SLP", "Loading epub for cover: %s (free heap: %d)", APP_STATE.openEpubPath.c_str(), ESP.getFreeHeap());
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }
    LOG_DBG("SLP", "Epub loaded, generating cover BMP (free heap: %d)", ESP.getFreeHeap());

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
    LOG_DBG("SLP", "Cover BMP path: %s", coverBmpPath.c_str());
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    const auto fileSize = file.size();
    LOG_DBG("SLP", "Cover BMP file size: %lu bytes", fileSize);
    Bitmap bitmap(file);
    const auto parseResult = bitmap.parseHeaders();
    if (parseResult == BmpReaderError::Ok) {
      // Validate file isn't truncated: check actual size against header + pixel data.
      // The prior constant `+ 70` over-estimated the 62-byte 1-bit BMP header by 8 bytes
      // and flagged every well-formed 1-bit cover (XTC, TXT) as truncated, deleting it
      // on every sleep cycle — broken since 1.0.0-ko.1. Use the real header offset from
      // the parsed BMP so 1-bit (62B), 8-bit grayscale (1078B), etc. all validate.
      const uint32_t expectedSize =
          bitmap.getDataOffset() + static_cast<uint32_t>(bitmap.getRowBytes()) * bitmap.getHeight();
      if (fileSize < expectedSize) {
        LOG_DBG("SLP", "Cover BMP truncated: %lu bytes < expected %lu, deleting", fileSize, expectedSize);
        file.close();
        Storage.remove(coverBmpPath.c_str());
        return (this->*renderNoCoverSleepScreen)();
      }
      LOG_DBG("SLP", "Rendering sleep cover: %s (%dx%d, %d bpp)", coverBmpPath.c_str(), bitmap.getWidth(),
              bitmap.getHeight(), bitmap.getBpp());
      renderBitmapSleepScreen(bitmap);
      return;
    }
    LOG_DBG("SLP", "Cover BMP parse failed: %s, deleting", Bitmap::errorToString(parseResult));
    Storage.remove(coverBmpPath.c_str());
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

// --- civil-date <-> unix-seconds conversion (no RTC/libc dependency) -------
// Howard Hinnant's algorithm; correct for years 0000..9999.
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t days, int& y, int& m, int& d) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t year0 = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
  y = static_cast<int>(year0 + (m <= 2));
}

// Unix-day -> weekday, 0=Sunday.
int weekdayFromDays(int64_t days) {
  // 1970-01-01 was a Thursday (4). weekday = (days + 4) mod 7.
  return static_cast<int>(((days % 7) + 7) % 7 + 4) % 7;
}

// Local (UTC-offset-applied) wall-clock fields.
struct LocalDateTime {
  int year, month, day, hour, minute, second, weekday;
};

LocalDateTime toLocal(const uint16_t year, const uint8_t month, const uint8_t day, const uint8_t hour,
                      const uint8_t minute, const uint8_t second, const int32_t offsetQuarterHours) {
  const int64_t days = daysFromCivil(year, month, day);
  int64_t secs = days * 86400LL + hour * 3600LL + minute * 60LL + second;
  secs += static_cast<int64_t>(offsetQuarterHours) * 15 * 60;  // apply zone offset

  // Re-derive civil fields from the shifted epoch.
  int64_t d = secs / 86400;
  const int64_t rem = secs % 86400;
  if (rem < 0) {
    --d;
  }
  int64_t s = rem;
  if (s < 0) s += 86400;
  LocalDateTime out;
  int y = 0, m = 0, dd = 0;
  civilFromDays(d, y, m, dd);
  out.year = y;
  out.month = m;
  out.day = dd;
  out.hour = static_cast<int>(s / 3600);
  out.minute = static_cast<int>((s / 60) % 60);
  out.second = static_cast<int>(s % 60);
  out.weekday = weekdayFromDays(d);
  return out;
}

// Vertical metrics of the two fixed built-in clock fonts, read from their
// EpdGlyph tables once and hardcoded so the per-minute repaint does no font
// lookups or map walks:
//   sleep_clock_100 (digits): glyph ink spans 152px above the baseline and
//     2px below it (height 154). ascender=199, advanceY=249.
//   sleep_clock_20 (date):   glyph ink spans 34px above the baseline and
//     3px below it (height 37). ascender=40, advanceY=50.
// drawText() places the BASELINE at its y argument.
static constexpr int CLOCK_INK_ABOVE = 152;  // digit ink above baseline
static constexpr int CLOCK_INK_BELOW = 2;    // digit ink below baseline
static constexpr int DATE_INK_ABOVE = 34;    // date ink above baseline
static constexpr int DATE_INK_BELOW = 3;     // date ink below baseline

void SleepActivity::renderClockSleepScreen() const {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (!quietRepaint) {
    renderer.clearScreen();
  }

  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0, weekday = 0;
  const bool haveDateTime = halClock.getDateTime(year, month, day, hour, minute, second, weekday);

  if (!haveDateTime) {
    // Fall back to a simple message when the RTC is absent/unreadable.
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No RTC");
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  // Apply the reader's clock-zone offset (SETTINGS.clockUtcOffsetQ: biased
  // quarter-hours, 48 = UTC+0) so the sleep clock matches the status bar.
  const int32_t offsetQ = static_cast<int32_t>(SETTINGS.clockUtcOffsetQ) - 48;
  const LocalDateTime lt = toLocal(year, month, day, hour, minute, second, offsetQ);

  char hourBuf[3], minBuf[3];
  snprintf(hourBuf, sizeof(hourBuf), "%02d", lt.hour);
  snprintf(minBuf, sizeof(minBuf), "%02d", lt.minute);

  static const StrId weekdayIds[7] = {
      StrId::STR_WEEKDAY_SUNDAY,   StrId::STR_WEEKDAY_MONDAY, StrId::STR_WEEKDAY_TUESDAY, StrId::STR_WEEKDAY_WEDNESDAY,
      StrId::STR_WEEKDAY_THURSDAY, StrId::STR_WEEKDAY_FRIDAY, StrId::STR_WEEKDAY_SATURDAY};
  const char* weekdayName = lt.weekday >= 0 && lt.weekday < 7 ? I18N.get(weekdayIds[lt.weekday]) : "";
  char dateBuf[64];
  snprintf(dateBuf, sizeof(dateBuf), tr(STR_SLEEP_CLOCK_DATE), lt.year % 10000, lt.month, lt.day, weekdayName);

  // Two-line clock: HH on top, a rule, MM below, then the date. drawText()
  // puts the BASELINE at its y argument; digit ink spans [y-152, y+2] and the
  // date ink [y-34, y+3]. All the y values below are baselines, and the gaps
  // are measured ink-edge to ink-edge so the digit boxes and the rule align.
  const int hhW = renderer.getTextWidth(SLEEP_CLOCK_FONT_ID, hourBuf);
  const int mmW = renderer.getTextWidth(SLEEP_CLOCK_FONT_ID, minBuf);
  const int dateW = renderer.getTextWidth(SLEEP_CLOCK_DATE_FONT_ID, dateBuf);
  const int xHH = std::max(0, (pageWidth - hhW) / 2);
  const int xMM = std::max(0, (pageWidth - mmW) / 2);
  const int xDate = std::max(0, (pageWidth - dateW) / 2);

  const int ruleGapAbove = 18;   // HH ink bottom -> rule
  const int ruleGapBelow = 120;  // rule -> MM ink top
  const int dateGap = 30;        // MM ink bottom -> date ink top

  // Whole block, ink-edge to ink-edge, centered on the panel.
  const int blockH = (CLOCK_INK_ABOVE + CLOCK_INK_BELOW) * 2 + ruleGapAbove + 4 + ruleGapBelow + dateGap +
                     DATE_INK_ABOVE + DATE_INK_BELOW;
  const int inkTop = std::max(0, (pageHeight - blockH) / 2);

  const int bHH = inkTop + CLOCK_INK_ABOVE;                            // HH baseline
  const int yRule = bHH + CLOCK_INK_BELOW + ruleGapAbove;              // just below HH ink
  const int bMM = yRule + 4 + ruleGapBelow + CLOCK_INK_ABOVE;          // MM baseline
  const int bDate = bMM + CLOCK_INK_BELOW + dateGap + DATE_INK_ABOVE;  // date baseline

  const int ruleW = std::max(hhW, mmW);
  const int xRule = (pageWidth - ruleW) / 2;

  if (quietRepaint) {
    // Partial repaint on timer wakes: erase only the regions that can have
    // changed (the two digit lines, the rule, and the date line).
    renderer.fillRect(xHH, bHH - CLOCK_INK_ABOVE, hhW, CLOCK_INK_ABOVE + CLOCK_INK_BELOW, false);
    renderer.fillRect(xMM, bMM - CLOCK_INK_ABOVE, mmW, CLOCK_INK_ABOVE + CLOCK_INK_BELOW, false);
    renderer.fillRect(xRule, yRule, ruleW, 4, false);
    // Full-width so a date rollover at midnight (wider new string) can't leave
    // stale pixels outside the freshly-drawn width.
    renderer.fillRect(0, bDate - DATE_INK_ABOVE, pageWidth, DATE_INK_ABOVE + DATE_INK_BELOW, false);
  }

  renderer.drawText(SLEEP_CLOCK_FONT_ID, xHH, bHH, hourBuf);
  renderer.drawText(SLEEP_CLOCK_FONT_ID, xMM, bMM, minBuf);

  renderer.fillRect(xRule, yRule, ruleW, 4, true);

  renderer.drawText(SLEEP_CLOCK_DATE_FONT_ID, xDate, bDate, dateBuf);

  renderer.displayBuffer(quietRepaint ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
}
