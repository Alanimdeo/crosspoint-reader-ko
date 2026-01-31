#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include "CategorySettingsActivity.h"
#include "CrossPointSettings.h"
#include "FontSelectionActivity.h"
#include "MappedInputManager.h"
#include "fontIds.h"

const char* SettingsActivity::categoryNames[categoryCount] = {"디스플레이", "리더", "컨트롤", "시스템"};

namespace {
constexpr int displaySettingsCount = 6;
const SettingInfo displaySettings[displaySettingsCount] = {
    // Should match with SLEEP_SCREEN_MODE (Korean)
    SettingInfo::Enum("절전 화면 이미지", &CrossPointSettings::sleepScreen,
                      {"다크", "라이트", "사용자 정의", "커버", "없음"}),
    SettingInfo::Enum("절전 화면 커버 모드", &CrossPointSettings::sleepScreenCoverMode, {"맞춤", "자르기"}),
    SettingInfo::Enum("절전 화면 커버 필터", &CrossPointSettings::sleepScreenCoverFilter,
                      {"없음", "대비", "반전"}),
    SettingInfo::Enum("상태 표시줄", &CrossPointSettings::statusBar,
                      {"없음", "진행 없음", "전체 w/ %", "전체 w/ 진행바", "진행바만"}),
    SettingInfo::Enum("배터리 % 숨기기", &CrossPointSettings::hideBatteryPercentage, {"안 함", "리더에서", "항상"}),
    SettingInfo::Enum("새로고침 주기", &CrossPointSettings::refreshFrequency,
                      {"1 페이지", "5 페이지", "10 페이지", "15 페이지", "30 페이지"})};

constexpr int readerSettingsCount = 12;
const SettingInfo readerSettings[readerSettingsCount] = {
    SettingInfo::Action("글꼴 설정"),
    SettingInfo::Enum("줄 간격", &CrossPointSettings::lineSpacing, {"좁게", "보통", "넓게"}),
    SettingInfo::Value("화면 여백", &CrossPointSettings::screenMargin, {5, 40, 5}),
    SettingInfo::Enum("문단 정렬", &CrossPointSettings::paragraphAlignment, {"양쪽 정렬", "왼쪽", "가운데", "오른쪽"}),
    SettingInfo::Toggle("하이픈 처리", &CrossPointSettings::hyphenationEnabled),
    SettingInfo::Enum("읽기 방향", &CrossPointSettings::orientation,
                      {"세로", "가로 시계방향", "반전", "가로 반시계방향"}),
    SettingInfo::Toggle("문단 간격 추가", &CrossPointSettings::extraParagraphSpacing),
    SettingInfo::Toggle("첫 줄 들여쓰기", &CrossPointSettings::paragraphIndent),
    SettingInfo::Toggle("문자 단위 줄바꿈", &CrossPointSettings::characterWrap),
    SettingInfo::Toggle("텍스트 안티앨리어싱", &CrossPointSettings::textAntiAliasing)};

constexpr int controlsSettingsCount = 4;
const SettingInfo controlsSettings[controlsSettingsCount] = {
    // Korean translations with new upstream option
    SettingInfo::Enum("앞면 버튼 레이아웃", &CrossPointSettings::frontButtonLayout,
                      {"뒤로, 확인, 왼쪽, 오른쪽", "왼쪽, 오른쪽, 뒤로, 확인", "왼쪽, 뒤로, 확인, 오른쪽", "뒤로, 확인, 오른쪽, 왼쪽"}),
    SettingInfo::Enum("측면 버튼 레이아웃 (리더기)", &CrossPointSettings::sideButtonLayout,
                      {"이전, 다음", "다음, 이전"}),
    SettingInfo::Toggle("길게 누르면 챕터 건너뛰기", &CrossPointSettings::longPressChapterSkip),
    SettingInfo::Enum("전원 버튼 짧게 누르기", &CrossPointSettings::shortPwrBtn, {"무시", "절전", "페이지 넘기기"})};

constexpr int systemSettingsCount = 5;
const SettingInfo systemSettings[systemSettingsCount] = {
    SettingInfo::Enum("절전 시간", &CrossPointSettings::sleepTimeout, {"1분", "5분", "10분", "15분", "30분"}),
    SettingInfo::Action("KOReader 동기화"), SettingInfo::Action("OPDS 브라우저"), SettingInfo::Action("캐시 지우기"),
    SettingInfo::Action("업데이트 확인")};
}  // namespace

void SettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsActivity*>(param);
  self->displayTaskLoop();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  // Reset selection to first category
  selectedCategoryIndex = 0;

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void SettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle category selection
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    enterCategory(selectedCategoryIndex);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  // Handle navigation
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    // Move selection up (with wrap-around)
    selectedCategoryIndex = (selectedCategoryIndex > 0) ? (selectedCategoryIndex - 1) : (categoryCount - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    // Move selection down (with wrap around)
    selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
    updateRequired = true;
  }
}

void SettingsActivity::enterCategory(int categoryIndex) {
  if (categoryIndex < 0 || categoryIndex >= categoryCount) {
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();

  const SettingInfo* settingsList = nullptr;
  int settingsCount = 0;

  switch (categoryIndex) {
    case 0:  // Display
      settingsList = displaySettings;
      settingsCount = displaySettingsCount;
      break;
    case 1:  // Reader
      settingsList = readerSettings;
      settingsCount = readerSettingsCount;
      break;
    case 2:  // Controls
      settingsList = controlsSettings;
      settingsCount = controlsSettingsCount;
      break;
    case 3:  // System
      settingsList = systemSettings;
      settingsCount = systemSettingsCount;
      break;
  }

  enterNewActivity(new CategorySettingsActivity(renderer, mappedInput, categoryNames[categoryIndex], settingsList,
                                                settingsCount, [this] {
                                                  exitActivity();
                                                  updateRequired = true;
                                                }));
  xSemaphoreGive(renderingMutex);
}

void SettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void SettingsActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  // renderer.drawCenteredText(UI_12_FONT_ID, 15, "Settings", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "설정", true, EpdFontFamily::BOLD);

  // Draw selection
  renderer.fillRect(0, 60 + selectedCategoryIndex * 30 - 2, pageWidth - 1, 30);

  // Draw all categories
  for (int i = 0; i < categoryCount; i++) {
    const int categoryY = 60 + i * 30;  // 30 pixels between categories

    // Draw category name
    renderer.drawText(UI_10_FONT_ID, 20, categoryY, categoryNames[i], i != selectedCategoryIndex);
  }

  // Draw version text above button hints
  renderer.drawText(SMALL_FONT_ID, pageWidth - 20 - renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION),
                    pageHeight - 60, CROSSPOINT_VERSION);

  // Draw help text
  const auto labels = mappedInput.mapLabels("« 뒤로", "선택", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
