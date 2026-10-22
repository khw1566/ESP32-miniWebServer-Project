#pragma once
#include <stddef.h>

// 1. 초기 접속 가이드 페이지 HTML
extern const char HTML_HELP_GUIDE[];

// 2. 메인 가동 중 쇼케이스 페이지 HTML
extern const char HTML_MAIN[];

// 3. AI 웹 생성 스튜디오 HTML
extern const char HTML_AI_STUDIO[];

// 4. 관리자 대시보드 HTML
extern const char HTML_ADMIN_DASHBOARD[];
extern const unsigned char HTML_ADMIN_DASHBOARD_GZIP[], HTML_AI_STUDIO_GZIP[];
extern const size_t HTML_ADMIN_DASHBOARD_GZIP_LEN, HTML_AI_STUDIO_GZIP_LEN;

// 5. 제품 기본 페이지: 내장 제품 페이지 데이터 (파일 저장소는 SD 전용) (web_page_factory.c)
extern const char FACTORY_INDEX_HTML[];
extern const size_t FACTORY_INDEX_HTML_LEN;
