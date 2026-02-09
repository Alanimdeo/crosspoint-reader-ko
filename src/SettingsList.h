#pragma once

#include <vector>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "activities/settings/SettingsActivity.h"

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
inline std::vector<SettingInfo> getSettingsList() {
  return {
      // --- Display ---
      SettingInfo::Enum("절전 화면 이미지", &CrossPointSettings::sleepScreen,
                        {"다크", "라이트", "사용자 정의", "커버", "없음", "커버 + 사용자 정의"}, "sleepScreen",
                        "Display"),
      SettingInfo::Enum("절전 화면 커버 모드", &CrossPointSettings::sleepScreenCoverMode, {"맞춤", "자르기"},
                        "sleepScreenCoverMode", "Display"),
      SettingInfo::Enum("절전 화면 커버 필터", &CrossPointSettings::sleepScreenCoverFilter, {"없음", "대비", "반전"},
                        "sleepScreenCoverFilter", "Display"),
      SettingInfo::Enum("상태 표시줄", &CrossPointSettings::statusBar,
                        {"없음", "진행 없음", "전체 w/ %", "전체 w/ 진행바", "진행바만", "전체 w/ 챕터바"}, "statusBar",
                        "Display"),
      SettingInfo::Enum("배터리 % 숨기기", &CrossPointSettings::hideBatteryPercentage, {"안 함", "리더에서", "항상"},
                        "hideBatteryPercentage", "Display"),
      SettingInfo::Enum("새로고침 주기", &CrossPointSettings::refreshFrequency,
                        {"1 페이지", "5 페이지", "10 페이지", "15 페이지", "30 페이지"}, "refreshFrequency", "Display"),
      SettingInfo::Enum("UI 테마", &CrossPointSettings::uiTheme, {"클래식", "Lyra"}, "uiTheme", "Display"),
      SettingInfo::Toggle("햇빛 바램 보정", &CrossPointSettings::fadingFix, "fadingFix", "Display"),

      // --- Reader ---
      SettingInfo::Enum("줄 간격", &CrossPointSettings::lineSpacing, {"좁게", "보통", "넓게"}, "lineSpacing", "Reader"),
      SettingInfo::Value("화면 여백", &CrossPointSettings::screenMargin, {5, 40, 5}, "screenMargin", "Reader"),
      SettingInfo::Enum("문단 정렬", &CrossPointSettings::paragraphAlignment,
                        {"양쪽 정렬", "왼쪽", "가운데", "오른쪽", "책 스타일"}, "paragraphAlignment", "Reader"),
      SettingInfo::Toggle("책 내장 스타일", &CrossPointSettings::embeddedStyle, "embeddedStyle", "Reader"),
      SettingInfo::Toggle("하이픈 처리", &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled", "Reader"),
      SettingInfo::Enum("읽기 방향", &CrossPointSettings::orientation,
                        {"세로", "가로 시계방향", "반전", "가로 반시계방향"}, "orientation", "Reader"),
      SettingInfo::Toggle("문단 간격 추가", &CrossPointSettings::extraParagraphSpacing, "extraParagraphSpacing",
                          "Reader"),
      SettingInfo::Toggle("첫 줄 들여쓰기", &CrossPointSettings::paragraphIndent, "paragraphIndent", "Reader"),
      SettingInfo::Toggle("문자 단위 줄바꿈", &CrossPointSettings::characterWrap, "characterWrap", "Reader"),
      SettingInfo::Toggle("텍스트 안티앨리어싱", &CrossPointSettings::textAntiAliasing, "textAntiAliasing", "Reader"),

      // --- Controls ---
      SettingInfo::Enum("측면 버튼 레이아웃 (리더기)", &CrossPointSettings::sideButtonLayout,
                        {"이전, 다음", "다음, 이전"}, "sideButtonLayout", "Controls"),
      SettingInfo::Toggle("길게 누르면 챕터 건너뛰기", &CrossPointSettings::longPressChapterSkip,
                          "longPressChapterSkip", "Controls"),
      SettingInfo::Enum("전원 버튼 짧게 누르기", &CrossPointSettings::shortPwrBtn, {"무시", "절전", "페이지 넘기기"},
                        "shortPwrBtn", "Controls"),

      // --- System ---
      SettingInfo::Enum("절전 시간", &CrossPointSettings::sleepTimeout, {"1분", "5분", "10분", "15분", "30분"},
                        "sleepTimeout", "System"),

      // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
      SettingInfo::DynamicString(
          "KOReader 사용자 이름", [] { return KOREADER_STORE.getUsername(); },
          [](const std::string& v) {
            KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
            KOREADER_STORE.saveToFile();
          },
          "koUsername", "KOReader Sync"),
      SettingInfo::DynamicString(
          "KOReader 비밀번호", [] { return KOREADER_STORE.getPassword(); },
          [](const std::string& v) {
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
            KOREADER_STORE.saveToFile();
          },
          "koPassword", "KOReader Sync"),
      SettingInfo::DynamicString(
          "동기화 서버 URL", [] { return KOREADER_STORE.getServerUrl(); },
          [](const std::string& v) {
            KOREADER_STORE.setServerUrl(v);
            KOREADER_STORE.saveToFile();
          },
          "koServerUrl", "KOReader Sync"),
      SettingInfo::DynamicEnum(
          "문서 매칭 방식", {"파일 이름", "바이너리"},
          [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
          [](uint8_t v) {
            KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
            KOREADER_STORE.saveToFile();
          },
          "koMatchMethod", "KOReader Sync"),

      // --- OPDS Browser (web-only, uses CrossPointSettings char arrays) ---
      SettingInfo::String("OPDS 서버 URL", SETTINGS.opdsServerUrl, sizeof(SETTINGS.opdsServerUrl), "opdsServerUrl",
                          "OPDS Browser"),
      SettingInfo::String("OPDS 사용자 이름", SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername), "opdsUsername",
                          "OPDS Browser"),
      SettingInfo::String("OPDS 비밀번호", SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword), "opdsPassword",
                          "OPDS Browser"),
  };
}
