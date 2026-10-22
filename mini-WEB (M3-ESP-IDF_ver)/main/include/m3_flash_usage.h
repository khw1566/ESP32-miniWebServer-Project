#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "m3_i18n.h"

/* Firmware Flash usage by function. tools/flash_usage.py reads this list, measures the
 * link map after every link and patches the measured bytes into the ELF before the
 * .bin is made. X(id, group, English, Korean, Japanese); keep ids stable and append. */
typedef enum { M3_FLASH_GROUP_APP, M3_FLASH_GROUP_ASSET, M3_FLASH_GROUP_CORE, M3_FLASH_GROUP_LIB,
    M3_FLASH_GROUP_OTHER, M3_FLASH_GROUP_COUNT } m3_flash_group_t;

#define M3_FLASH_PARTS(X) \
    X(APP_UI, APP, "Touch UI (home, app screens)", "터치 UI (홈·앱 화면)", "タッチUI (ホーム・アプリ画面)") \
    X(APP_WEB, APP, "Web server and admin", "웹 서버·관리", "Webサーバー・管理") \
    X(APP_AI, APP, "AI agent and voice input", "AI 에이전트·음성 입력", "AIエージェント・音声入力") \
    X(APP_TTS, APP, "Speech engine (eSpeak)", "음성 합성 엔진 (eSpeak)", "音声合成エンジン (eSpeak)") \
    X(APP_AUDIO, APP, "Music, radio, audio codecs", "음악·라디오·오디오 코덱", "音楽・ラジオ・音声コーデック") \
    X(APP_VIDEO, APP, "Video and YouTube", "영상·유튜브", "動画・YouTube") \
    X(APP_PHOTO, APP, "Photos and gallery", "사진·갤러리", "写真・ギャラリー") \
    X(APP_READER, APP, "Text reader", "텍스트 리더", "テキストリーダー") \
    X(APP_TOOLS, APP, "Games and tools", "게임·도구", "ゲーム・ツール") \
    X(APP_LUA, APP, "SD apps (Lua)", "SD 앱 (Lua)", "SDアプリ (Lua)") \
    X(AST_VOICE, ASSET, "Speech voice data", "음성 합성 데이터", "音声合成データ") \
    X(AST_FONT_KO, ASSET, "Korean font", "한글 글꼴", "ハングルフォント") \
    X(AST_FONT_CJK, ASSET, "Kanji and kana font", "한자·일본어 글꼴", "漢字・かなフォント") \
    X(AST_FONT_UI, ASSET, "Other UI fonts", "기타 화면 글꼴", "その他の画面フォント") \
    X(AST_WEB, ASSET, "Web page files", "웹 페이지 파일", "Webページファイル") \
    X(AST_BOOT, ASSET, "Boot screen", "부팅 화면", "起動画面") \
    X(AST_TEXT, ASSET, "UI text and translations", "화면 문구·번역", "画面の文言・翻訳") \
    X(AST_STRINGS, ASSET, "Shared strings and logs", "공용 문자열·로그", "共有文字列・ログ") \
    X(AST_CERTS, ASSET, "TLS certificate bundle", "TLS 인증서 묶음", "TLS証明書バンドル") \
    X(CORE_MAIN, CORE, "Device core", "기기 코어", "デバイスコア") \
    X(LIB_LVGL, LIB, "LVGL graphics", "LVGL 그래픽", "LVGLグラフィック") \
    X(LIB_NET, LIB, "Network (Wi-Fi, lwIP, HTTP)", "네트워크 (Wi-Fi·lwIP·HTTP)", "ネットワーク (Wi-Fi・lwIP・HTTP)") \
    X(LIB_TLS, LIB, "Security and crypto", "보안·암호화", "セキュリティ・暗号") \
    X(LIB_LIBC, LIB, "C runtime libraries", "C 표준 라이브러리", "C標準ライブラリ") \
    X(LIB_IDF, LIB, "ESP-IDF system and drivers", "ESP-IDF 시스템·드라이버", "ESP-IDFシステム・ドライバ") \
    X(OTHER, OTHER, "Alignment and other", "정렬·기타", "アライメント・その他")

#define M3_FLASH_PART_ENUM(id, group, en, ko, ja) M3_FLASH_##id,
typedef enum { M3_FLASH_PARTS(M3_FLASH_PART_ENUM) M3_FLASH_PART_COUNT } m3_flash_part_t;
#undef M3_FLASH_PART_ENUM

typedef struct {
    bool measured;          /* false until tools/flash_usage.py patched this image */
    uint32_t image_bytes;   /* bytes of loadable sections in the app image */
    uint32_t slot_bytes;    /* size of the running OTA app partition; 0 if unknown */
    uint32_t part_bytes[M3_FLASH_PART_COUNT];
} m3_flash_usage_t;

void m3_flash_usage_get(m3_flash_usage_t *out);
m3_flash_group_t m3_flash_usage_group(m3_flash_part_t part);
const char *m3_flash_usage_part_name(m3_flash_part_t part, m3_lang_t language);
const char *m3_flash_usage_group_name(m3_flash_group_t group, m3_lang_t language);
