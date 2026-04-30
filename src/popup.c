/*
 * popup.c — Dark-themed popup window
 * ====================================
 * Pure Win32 — no external UI library.
 * Matches the colour scheme of the Python/extension version.
 */

#include "../include/app.h"

/* ── Module-level state ─────────────────────────────────────── */
static HINSTANCE g_hInst     = NULL;
static HWND      g_hwnd      = NULL;   /* NULL = popup not open */
static HBRUSH    g_brBg      = NULL;   /* background colour brush */
static HBRUSH    g_brDark    = NULL;   /* dark-panel colour brush */
static HFONT     g_fontUI    = NULL;   /* Segoe UI 10pt           */
static HFONT     g_fontCode  = NULL;   /* Consolas 10pt           */
static char      g_apiKey[256] = {0};

/* ── Button descriptor table ────────────────────────────────── */
typedef struct { int id; const char *label; COLORREF clr; const char *mode; } Btn;
static const Btn g_btns[4] = {
    { ID_BTN_EXPLAIN,  "ELI10",    C_EXPLAIN,  "explain"  },
    { ID_BTN_HINT,     "Hint",     C_HINT,     "hint"     },
    { ID_BTN_FIX,      "Fix",      C_FIX,      "fix"      },
    { ID_BTN_SOLUTION, "Solution", C_SOLUTION, "solution" },
};

/* ── Helper: write text to an EDIT control ──────────────────────
   Win32 EDIT controls use \r\n; convert \n → \r\n first.        */
static void SetEditText(int ctlId, const char *text) {
    if (!g_hwnd) return;
    size_t n   = strlen(text);
    char  *buf = malloc(n * 2 + 2);
    char  *q   = buf;
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '\n' && (i == 0 || text[i-1] != '\r'))
            *q++ = '\r';
        *q++ = text[i];
    }
    *q = '\0';
    SetDlgItemTextA(g_hwnd, ctlId, buf);
    free(buf);
}

static void SetButtonsEnabled(BOOL on) {
    for (int i = 0; i < 4; i++)
        EnableWindow(GetDlgItem(g_hwnd, g_btns[i].id), on);
}

/* ── Window procedure ───────────────────────────────────────── */
static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    /* ── Build child controls on creation ─────────────────────── */
    case WM_CREATE: {
        int pad = 14;
        RECT cr; GetClientRect(hwnd, &cr);
        int W = cr.right;
        int y = pad;

        /* Header label */
        HWND h = CreateWindowExA(0, "STATIC", APP_NAME,
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            pad, y, W - pad*2, 22, hwnd,
            (HMENU)(intptr_t)ID_LBL_HEADER, g_hInst, NULL);
        SendMessageA(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        y += 28;

        /* Thin separator */
        CreateWindowExA(0, "STATIC", "",
            WS_CHILD|WS_VISIBLE|SS_SUNKEN,
            pad, y, W - pad*2, 1, hwnd, NULL, g_hInst, NULL);
        y += 8;

        /* Input label */
        h = CreateWindowExA(0, "STATIC", "CODE / QUESTION",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            pad, y, W - pad*2, 16, hwnd, NULL, g_hInst, NULL);
        SendMessageA(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        y += 20;

        /* Code input box */
        h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,
            pad, y, W - pad*2, 120, hwnd,
            (HMENU)(intptr_t)ID_EDIT_INPUT, g_hInst, NULL);
        SendMessageA(h, WM_SETFONT, (WPARAM)g_fontCode, TRUE);
        y += 128;

        /* Four coloured action buttons */
        int bw = (W - pad*2 - 6) / 4;
        for (int i = 0; i < 4; i++) {
            CreateWindowExA(0, "BUTTON", g_btns[i].label,
                WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP,
                pad + i*(bw+2), y, bw, 32, hwnd,
                (HMENU)(intptr_t)g_btns[i].id, g_hInst, NULL);
        }
        y += 40;

        /* Separator */
        CreateWindowExA(0, "STATIC", "",
            WS_CHILD|WS_VISIBLE|SS_SUNKEN,
            pad, y, W - pad*2, 1, hwnd, NULL, g_hInst, NULL);
        y += 8;

        /* Response label */
        h = CreateWindowExA(0, "STATIC", "RESPONSE",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            pad, y, W - pad*2, 16, hwnd, NULL, g_hInst, NULL);
        SendMessageA(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        y += 20;

        /* Response area (read-only, fills remaining height) */
        h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT",
            "Select text in any app and press Ctrl+Alt+C,\r\n"
            "or type your question above and click a button.",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,
            pad, y, W - pad*2, cr.bottom - y - pad, hwnd,
            (HMENU)(intptr_t)ID_EDIT_RESP, g_hInst, NULL);
        SendMessageA(h, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        return 0;
    }

    /* ── Resize: keep response box filling the bottom ─────────── */
    case WM_SIZE: {
        int W = LOWORD(lParam), H = HIWORD(lParam);
        int pad = 14;
        /* response EDIT is at y ≈ 280 — just stretch it to the new bottom */
        HWND hResp = GetDlgItem(hwnd, ID_EDIT_RESP);
        if (hResp) {
            RECT r; GetWindowRect(hResp, &r);
            POINT pt = { r.left, r.top };
            ScreenToClient(hwnd, &pt);
            SetWindowPos(hResp, NULL,
                pt.x, pt.y,
                W - pad*2, H - pt.y - pad,
                SWP_NOZORDER);
        }
        return 0;
    }

    /* ── Button click ─────────────────────────────────────────── */
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        for (int i = 0; i < 4; i++) {
            if (id != g_btns[i].id) continue;

            char query[8192] = {0};
            GetDlgItemTextA(hwnd, ID_EDIT_INPUT, query, sizeof(query));
            if (!query[0]) {
                SetEditText(ID_EDIT_RESP,
                    "Please enter a question or paste some code first.");
                return 0;
            }

            /* Show loading message, disable buttons */
            SetButtonsEnabled(FALSE);
            const char *loading[4] = {
                "Explaining...", "Thinking...",
                "Fixing C code...", "Solving in C..."
            };
            SetEditText(ID_EDIT_RESP, loading[i]);

            /* Launch worker thread — it will PostMessage when done */
            ApiParams *p = malloc(sizeof(ApiParams));
            p->hwnd = hwnd;
            strncpy(p->query,  query,        sizeof(p->query)  - 1);
            strncpy(p->mode,   g_btns[i].mode, sizeof(p->mode) - 1);
            strncpy(p->apiKey, g_apiKey,     sizeof(p->apiKey) - 1);
            p->query [sizeof(p->query)  - 1] = '\0';
            p->mode  [sizeof(p->mode)   - 1] = '\0';
            p->apiKey[sizeof(p->apiKey) - 1] = '\0';

            HANDLE hT = CreateThread(NULL, 0, ApiThread, p, 0, NULL);
            if (hT) CloseHandle(hT);
            else {
                free(p);
                SetEditText(ID_EDIT_RESP, "Failed to start API thread.");
                SetButtonsEnabled(TRUE);
            }
            return 0;
        }
        return 0;
    }

    /* ── Groq response arrived ────────────────────────────────── */
    case WM_API_DONE: {
        char *text = (char *)lParam;
        SetEditText(ID_EDIT_RESP, text);
        free(text);
        SetButtonsEnabled(TRUE);
        return 0;
    }

    case WM_API_FAIL: {
        char *err = (char *)lParam;
        SetEditText(ID_EDIT_RESP, err);
        free(err);
        SetButtonsEnabled(TRUE);
        return 0;
    }

    /* ── Owner-draw coloured buttons ─────────────────────────── */
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lParam;
        for (int i = 0; i < 4; i++) {
            if (d->CtlID != (UINT)g_btns[i].id) continue;

            COLORREF clr = (d->itemState & ODS_DISABLED) ? C_MUTED : g_btns[i].clr;
            HBRUSH br = CreateSolidBrush(clr);
            FillRect(d->hDC, &d->rcItem, br);
            DeleteObject(br);

            SetTextColor(d->hDC, C_BTN_FG);
            SetBkMode(d->hDC, TRANSPARENT);
            SelectObject(d->hDC, g_fontUI);
            DrawTextA(d->hDC, g_btns[i].label, -1, &d->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        return 0;
    }

    /* ── Dark theme colouring ─────────────────────────────────── */
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, C_BG_DARK);
        SetTextColor(hdc, C_TEXT);
        return (LRESULT)g_brDark;
    }

    case WM_CTLCOLORSTATIC: {
        HDC  hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        SetBkColor(hdc, C_BG);
        if (GetDlgCtrlID(hCtl) == ID_LBL_HEADER)
            SetTextColor(hdc, C_ACCENT);
        else
            SetTextColor(hdc, C_MUTED);
        return (LRESULT)g_brBg;
    }

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_brBg);
        return TRUE;
    }

    /* ── Close hides rather than destroys ────────────────────── */
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        g_hwnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ── One-time setup called from WinMain ─────────────────────── */
void RegisterPopupClass(HINSTANCE hInst) {
    g_hInst = hInst;

    /* Load API key */
    char *key = ReadApiKey();
    if (key) {
        strncpy(g_apiKey, key, sizeof(g_apiKey) - 1);
        free(key);
    }

    /* GDI resources */
    g_brBg   = CreateSolidBrush(C_BG);
    g_brDark = CreateSolidBrush(C_BG_DARK);

    g_fontUI = CreateFontA(-MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_SWISS, "Segoe UI");

    g_fontCode = CreateFontA(-MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH|FF_MODERN, "Consolas");

    /* Register window class */
    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = PopupProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = "CAssistantPopup";
    RegisterClassExA(&wc);
}

/* ── Show (or bring to front) the popup ─────────────────────── */
void ShowOrUpdatePopup(HINSTANCE hInst, const char *text) {
    if (!g_hwnd || !IsWindow(g_hwnd)) {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        int w = 460, h = 580;

        g_hwnd = CreateWindowExA(
            WS_EX_TOPMOST,
            "CAssistantPopup", APP_NAME,
            (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) | WS_VISIBLE,
            (sw - w) / 2, (sh - h) / 2, w, h,
            NULL, NULL, hInst, NULL);

        if (!g_apiKey[0]) {
            SetEditText(ID_EDIT_RESP,
                "No API key found.\r\n\r\n"
                "Create a file called  .env  next to  c-assistant.exe  with:\r\n\r\n"
                "    GROQ_API_KEY=your_key_here\r\n\r\n"
                "Get a free key at: https://console.groq.com/keys");
        }
    }

    if (text && text[0])
        SetDlgItemTextA(g_hwnd, ID_EDIT_INPUT, text);

    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_hwnd);
}
