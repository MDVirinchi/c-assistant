/*
 * main.c — WinMain, system tray icon, global hotkey, message loop
 * =================================================================
 * This is the entry point. It:
 *   1. Creates a hidden window to receive tray + hotkey messages
 *   2. Adds the icon to the system tray
 *   3. Registers Ctrl+Alt+C as a global hotkey
 *   4. On hotkey: captures selected text, opens the popup
 *   5. Runs the Win32 message loop until "Quit" is chosen
 */

#include "../include/app.h"

static HINSTANCE      g_hInst    = NULL;
static HWND           g_hwndMain = NULL;
static NOTIFYICONDATAA g_nid     = {0};
static HICON          g_hIcon    = NULL;

/* ── Create the tray icon in code (no .ico file needed) ─────────
   Draws a dark circle with a blue "C" arc — same idea as Python. */
HICON MakeTrayIcon(void) {
    int sz = 32;

    HDC     hdcScr = GetDC(NULL);
    HDC     hdcMem = CreateCompatibleDC(hdcScr);
    HBITMAP hBmp   = CreateCompatibleBitmap(hdcScr, sz, sz);
    HBITMAP hOld   = SelectObject(hdcMem, hBmp);

    /* Fill dark background */
    HBRUSH bgBr = CreateSolidBrush(C_BG);
    RECT   rc   = {0, 0, sz, sz};
    FillRect(hdcMem, &rc, bgBr);
    DeleteObject(bgBr);

    /* Draw the "C" arc in accent colour */
    HPEN pen    = CreatePen(PS_SOLID, 4, C_ACCENT);
    HPEN oldPen = SelectObject(hdcMem, pen);
    SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
    /* Arc(hdc, left,top,right,bottom, xStart,yStart, xEnd,yEnd)
       Open on the right → start upper-right, end lower-right     */
    Arc(hdcMem, 5, 5, sz-5, sz-5,   sz-9, 7,   sz-9, sz-7);
    SelectObject(hdcMem, oldPen);
    DeleteObject(pen);

    SelectObject(hdcMem, hOld);

    /* Mask bitmap (all-black = colour icon fully visible) */
    HBITMAP hMask = CreateBitmap(sz, sz, 1, 1, NULL);

    ICONINFO ii  = {0};
    ii.fIcon     = TRUE;
    ii.hbmColor  = hBmp;
    ii.hbmMask   = hMask;
    HICON hIcon  = CreateIconIndirect(&ii);

    DeleteObject(hBmp);
    DeleteObject(hMask);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScr);

    return hIcon;
}

/* ── Add / remove the tray icon ─────────────────────────────── */
static void TrayAdd(void) {
    g_nid.cbSize           = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd             = g_hwndMain;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_MSG;
    g_nid.hIcon            = g_hIcon;
    strncpy(g_nid.szTip, APP_NAME " (Ctrl+Alt+C)", sizeof(g_nid.szTip) - 1);
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void TrayRemove(void) {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

/* ── Right-click context menu on the tray icon ──────────────── */
static void ShowTrayMenu(void) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING, IDM_OPEN, "Open  (Ctrl+Alt+C)");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, IDM_QUIT, "Quit");

    /* Required so the menu dismisses correctly */
    SetForegroundWindow(g_hwndMain);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwndMain, NULL);
    DestroyMenu(hMenu);
}

/* ── Read current clipboard text ────────────────────────────── */
static char *ClipboardRead(void) {
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE h = GetClipboardData(CF_TEXT);
    if (!h) { CloseClipboard(); return NULL; }
    char *src  = GlobalLock(h);
    char *copy = src ? strdup(src) : NULL;
    GlobalUnlock(h);
    CloseClipboard();
    return copy;
}

/* ── Hidden main window — receives tray + hotkey messages ───── */
static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_HOTKEY: {
        if (wParam != HOTKEY_ID) break;

        /* Save old clipboard so we can detect if selection changed */
        char *before = ClipboardRead();

        /* Simulate Ctrl+C in the active window */
        INPUT keys[4] = {0};
        keys[0].type    = INPUT_KEYBOARD; keys[0].ki.wVk = VK_CONTROL;
        keys[1].type    = INPUT_KEYBOARD; keys[1].ki.wVk = 'C';
        keys[2].type    = INPUT_KEYBOARD; keys[2].ki.wVk = 'C';
        keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
        keys[3].type    = INPUT_KEYBOARD; keys[3].ki.wVk = VK_CONTROL;
        keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, keys, sizeof(INPUT));

        Sleep(200);   /* give the app time to copy */

        char *after    = ClipboardRead();
        const char *selected = "";

        /* Only use clipboard if something new was copied */
        if (after && (!before || strcmp(after, before) != 0))
            selected = after;

        ShowOrUpdatePopup(g_hInst, selected);

        if (before) free(before);
        if (after)  free(after);
        return 0;
    }

    case WM_TRAY_MSG: {
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
            ShowTrayMenu();
            break;
        case WM_LBUTTONDBLCLK:
            ShowOrUpdatePopup(g_hInst, "");
            break;
        }
        return 0;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDM_OPEN: ShowOrUpdatePopup(g_hInst, ""); break;
        case IDM_QUIT: PostQuitMessage(0);             break;
        }
        return 0;
    }

    case WM_DESTROY:
        TrayRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ── Entry point ────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd; (void)nShow;
    g_hInst = hInst;

    /* Prevent running twice */
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "CAssistant_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(NULL,
            "C Assistant is already running.\n"
            "Look for its icon in the system tray (bottom-right).",
            APP_NAME, MB_ICONINFORMATION);
        CloseHandle(hMutex);
        return 0;
    }

    /* Register hidden window class */
    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "CAssistantHidden";
    RegisterClassExA(&wc);

    /* Create the hidden message-only window */
    g_hwndMain = CreateWindowExA(0, "CAssistantHidden", APP_NAME,
        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
        HWND_MESSAGE,   /* message-only: invisible, no taskbar entry */
        NULL, hInst, NULL);

    /* Tray icon */
    g_hIcon = MakeTrayIcon();
    TrayAdd();

    /* Global hotkey: Ctrl + Alt + C */
    if (!RegisterHotKey(g_hwndMain, HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'C')) {
        MessageBoxA(NULL,
            "Could not register Ctrl+Alt+C.\n"
            "Another application may already be using this hotkey.\n\n"
            "The app will still work via the tray icon.",
            APP_NAME, MB_ICONWARNING);
    }

    /* Register the popup window class */
    RegisterPopupClass(hInst);

    /* Message loop */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* Cleanup */
    UnregisterHotKey(g_hwndMain, HOTKEY_ID);
    if (g_hIcon) DestroyIcon(g_hIcon);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return (int)msg.wParam;
}
