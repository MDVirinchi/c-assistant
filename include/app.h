/*
 * app.h — Shared declarations for C Assistant Native
 * ====================================================
 * All four source files include this header.
 * No external libraries — only Windows built-ins.
 */

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── App identity ───────────────────────────────────────────── */
#define APP_NAME        "C Assistant"
#define HOTKEY_ID       1

/* ── Custom window messages ─────────────────────────────────── */
#define WM_TRAY_MSG     (WM_APP + 1)   /* tray icon events       */
#define WM_API_DONE     (WM_APP + 2)   /* Groq response ready    */
#define WM_API_FAIL     (WM_APP + 3)   /* Groq request failed    */

/* ── Menu IDs ───────────────────────────────────────────────── */
#define IDM_OPEN        101
#define IDM_QUIT        102

/* ── Child control IDs ──────────────────────────────────────── */
#define ID_LBL_HEADER   200
#define ID_EDIT_INPUT   201
#define ID_EDIT_RESP    202
#define ID_BTN_EXPLAIN  203
#define ID_BTN_HINT     204
#define ID_BTN_FIX      205
#define ID_BTN_SOLUTION 206

/* ── Colours (match the Python/extension theme exactly) ─────── */
#define C_BG        RGB(26,  26,  46 )
#define C_BG_DARK   RGB(15,  15,  26 )
#define C_BORDER    RGB(68,  68,  68 )
#define C_TEXT      RGB(224, 224, 224)
#define C_ACCENT    RGB(160, 196, 255)
#define C_MUTED     RGB(136, 136, 136)
#define C_ERROR     RGB(239, 154, 154)
#define C_EXPLAIN   RGB(79,  195, 247)
#define C_HINT      RGB(129, 199, 132)
#define C_FIX       RGB(255, 183, 77 )
#define C_SOLUTION  RGB(206, 147, 216)
#define C_BTN_FG    RGB(26,  26,  46 )

/* ── Payload passed to the Groq API worker thread ───────────── */
typedef struct {
    HWND hwnd;
    char query[8192];
    char mode[32];
    char apiKey[256];
} ApiParams;

/* ── config.c ───────────────────────────────────────────────── */
char *ReadApiKey(void);                /* returns heap string, caller frees */

/* ── groq.c ─────────────────────────────────────────────────── */
char          *CallGroq(const char *query, const char *mode,
                        const char *apiKey);
DWORD WINAPI   ApiThread(LPVOID lpParam);

/* ── popup.c ────────────────────────────────────────────────── */
void RegisterPopupClass(HINSTANCE hInst);
void ShowOrUpdatePopup(HINSTANCE hInst, const char *selectedText);

/* ── main.c ─────────────────────────────────────────────────── */
HICON MakeTrayIcon(void);
