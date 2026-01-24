#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>

#include <cstring>

#include "CalibreSettingsActivity.h"
#include "CrossPointSettings.h"
#include "FontSelectionActivity.h"
#include "MappedInputManager.h"
#include "OtaUpdateActivity.h"
#include "fontIds.h"

// Define the static settings list
namespace {
constexpr int settingsCount = 21;
const SettingInfo settingsList[settingsCount] = {
    // Should match with SLEEP_SCREEN_MODE
    SettingInfo::Enum("절전 화면 이미지", &CrossPointSettings::sleepScreen,
                      {"다크", "라이트", "사용자 정의", "커버", "없음"}),
    SettingInfo::Enum("절전 화면 커버 모드", &CrossPointSettings::sleepScreenCoverMode, {"맞춤", "자르기"}),
    SettingInfo::Enum("상태 표시줄", &CrossPointSettings::statusBar, {"없음", "진행 없음", "전체"}),
    SettingInfo::Enum("배터리 % 숨기기", &CrossPointSettings::hideBatteryPercentage, {"안 함", "리더에서", "항상"}),
    SettingInfo::Toggle("문단 간격 추가", &CrossPointSettings::extraParagraphSpacing),
    SettingInfo::Toggle("첫 줄 들여쓰기", &CrossPointSettings::paragraphIndent),
    SettingInfo::Toggle("텍스트 안티앨리어싱", &CrossPointSettings::textAntiAliasing),
    SettingInfo::Enum("전원 버튼 짧게 누르기", &CrossPointSettings::shortPwrBtn, {"무시", "절전", "페이지 넘기기"}),
    SettingInfo::Enum("읽기 방향", &CrossPointSettings::orientation,
                      {"세로", "가로 시계방향", "반전", "가로 반시계방향"}),
    SettingInfo::Enum("앞면 버튼 레이아웃", &CrossPointSettings::frontButtonLayout,
                      {"뒤로, 확인, 왼쪽, 오른쪽", "왼쪽, 오른쪽, 뒤로, 확인", "왼쪽, 뒤로, 확인, 오른쪽"}),
    SettingInfo::Enum("측면 버튼 레이아웃 (리더기)", &CrossPointSettings::sideButtonLayout,
                      {"이전, 다음", "다음, 이전"}),
    SettingInfo::Toggle("길게 누르면 챕터 건너뛰기", &CrossPointSettings::longPressChapterSkip),
    SettingInfo::Enum("줄 간격", &CrossPointSettings::lineSpacing, {"좁게", "보통", "넓게"}),
    SettingInfo::Value("화면 여백", &CrossPointSettings::screenMargin, {5, 40, 5}),
    SettingInfo::Enum("문단 정렬", &CrossPointSettings::paragraphAlignment, {"양쪽 정렬", "왼쪽", "가운데", "오른쪽"}),
    SettingInfo::Toggle("문자 단위 줄바꿈", &CrossPointSettings::characterWrap),
    SettingInfo::Enum("절전 시간", &CrossPointSettings::sleepTimeout, {"1분", "5분", "10분", "15분", "30분"}),
    SettingInfo::Enum("새로고침 주기", &CrossPointSettings::refreshFrequency,
                      {"1 페이지", "5 페이지", "10 페이지", "15 페이지", "30 페이지"}),
    SettingInfo::Action("글꼴 설정"),
    SettingInfo::Action("Calibre 설정"),
    SettingInfo::Action("업데이트 확인"),
};
}  // namespace

void SettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsActivity*>(param);
  self->displayTaskLoop();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  // Reset selection to first item
  selectedSettingIndex = 0;

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

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    updateRequired = true;
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
    selectedSettingIndex = (selectedSettingIndex > 0) ? (selectedSettingIndex - 1) : (settingsCount - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    // Move selection down (with wrap around)
    selectedSettingIndex = (selectedSettingIndex < settingsCount - 1) ? (selectedSettingIndex + 1) : 0;
    updateRequired = true;
  }
}

void SettingsActivity::toggleCurrentSetting() {
  // Validate index
  if (selectedSettingIndex < 0 || selectedSettingIndex >= settingsCount) {
    return;
  }

  const auto& setting = settingsList[selectedSettingIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    // Decreasing would also be nice for large ranges I think but oh well can't have everything
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    // Wrap to minValue if exceeding setting value boundary
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    if (strcmp(setting.name, "글꼴 설정") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new FontSelectionActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    } else if (strcmp(setting.name, "Calibre 설정") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new CalibreSettingsActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    } else if (strcmp(setting.name, "업데이트 확인") == 0) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new OtaUpdateActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    }
  } else {
    // Only toggle if it's a toggle type and has a value pointer
    return;
  }

  // Save settings when they change
  SETTINGS.saveToFile();
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
  renderer.fillRect(0, 60 + selectedSettingIndex * 30 - 2, pageWidth - 1, 30);

  // Draw all settings
  for (int i = 0; i < settingsCount; i++) {
    const int settingY = 60 + i * 30;  // 30 pixels between settings

    // Draw setting name
    renderer.drawText(UI_10_FONT_ID, 20, settingY, settingsList[i].name, i != selectedSettingIndex);

    // Draw value based on setting type
    std::string valueText = "";
    if (settingsList[i].type == SettingType::TOGGLE && settingsList[i].valuePtr != nullptr) {
      const bool value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = value ? "ON" : "OFF";
    } else if (settingsList[i].type == SettingType::ENUM && settingsList[i].valuePtr != nullptr) {
      const uint8_t value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = settingsList[i].enumValues[value];
    } else if (settingsList[i].type == SettingType::VALUE && settingsList[i].valuePtr != nullptr) {
      valueText = std::to_string(SETTINGS.*(settingsList[i].valuePtr));
    } else if (settingsList[i].type == SettingType::ACTION && strcmp(settingsList[i].name, "글꼴 설정") == 0) {
      // Show current font name for font settings
      valueText = SETTINGS.getCustomFontName();
    }
    const auto width = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
    renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - width, settingY, valueText.c_str(), i != selectedSettingIndex);
  }

  // Draw version text above button hints
  renderer.drawText(SMALL_FONT_ID, pageWidth - 20 - renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION),
                    pageHeight - 60, CROSSPOINT_VERSION);

  // Draw help text
  // const auto labels = mappedInput.mapLabels("« Save", "Toggle", "", "");
  const auto labels = mappedInput.mapLabels("« 저장", "토글", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
