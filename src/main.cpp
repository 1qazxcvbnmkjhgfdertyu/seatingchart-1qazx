#include "SeatingChartApp.h"
#include "CrashLog.h"
#include <windows.h>
#include <commctrl.h>
#include <string>

// ---------------------------------------------------------------------------
// Bundled font loader
// Load Baloo2.ttf from the "fonts" subfolder next to the executable.
// Using FR_PRIVATE keeps the font scoped to this process only — no system-wide
// registration, no cleanup needed on other apps, removed automatically when
// the process exits (Windows also removes it on RemoveFontResourceExW).
// ---------------------------------------------------------------------------
static std::wstring g_fontPath; // path kept so we can remove it on exit

static void LoadBundledFonts() {
    wchar_t exePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;

    // Strip the exe filename to get the directory
    std::wstring dir(exePath);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash + 1);

    g_fontPath = dir + L"fonts\\Baloo2.ttf";

    if (GetFileAttributesW(g_fontPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_fontPath.clear(); // font file not found — fall back gracefully
        return;
    }

    AddFontResourceExW(g_fontPath.c_str(), FR_PRIVATE, nullptr);
}

static void UnloadBundledFonts() {
    if (!g_fontPath.empty())
        RemoveFontResourceExW(g_fontPath.c_str(), FR_PRIVATE, nullptr);
}

// ---------------------------------------------------------------------------
// WndProc trampoline
// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* app = new SeatingChartApp();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return app->HandleMessage(hwnd, msg, wParam, lParam);
    }
    auto* app = SeatingChartApp::FromHwnd(hwnd);
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);
    if (msg == WM_DESTROY) {
        const LRESULT r = app->HandleMessage(hwnd, msg, wParam, lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete app;
        return r;
    }
    return app->HandleMessage(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK SidebarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<SeatingChartApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return app->HandleSidebarMessage(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    SetUnhandledExceptionFilter(SeatingChartUnhandledExceptionFilter);
    WriteAppLog(L"Application starting");
    SetProcessDPIAware();
    LoadBundledFonts(); // load Baloo 2 before any window/font is created

    WNDCLASSW wc{};
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = instance;
    wc.lpszClassName = L"SeatingChartWindow";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return 1;

    WNDCLASSW sc{};
    sc.lpfnWndProc   = SidebarWndProc;
    sc.hInstance     = instance;
    sc.lpszClassName = L"SeatingChartSidebar";
    sc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&sc)) return 1;

    HWND hwnd = CreateWindowExW(0, L"SeatingChartWindow", L"Seating Chart App",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 1360, 900,
                                 nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Pretranslate: forward selected shortcuts to the main window even when
        // a child control has keyboard focus, without disturbing edit-control text ops.
        if (msg.message == WM_KEYDOWN && IsChild(hwnd, msg.hwnd)) {
            const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
            const WPARAM vk  = msg.wParam;

            wchar_t cls[64]{};
            GetClassNameW(GetFocus(), cls, 64);
            const bool inEdit = (_wcsicmp(cls, L"Edit") == 0);

            bool intercept = false;
            if (ctrl  && vk == 'S')                           intercept = true; // Save
            if (ctrl  && vk == 'E')                           intercept = true; // Export
            if (ctrl  && vk == 'P')                           intercept = true; // Print
            if (ctrl  && !inEdit && vk == 'C')                intercept = true; // Copy chart
            if (ctrl  && !inEdit && vk == 'X')                intercept = true; // Cut layout items
            if (ctrl  && !inEdit && vk == 'V')                intercept = true; // Paste layout items
            if (ctrl  && !inEdit && vk == 'Z')                intercept = true; // Undo
            if (ctrl  && !inEdit && (vk == 'Y' || (shift && vk == 'Z'))) intercept = true; // Redo
            if (!inEdit && (vk == VK_DELETE || vk == VK_BACK)) intercept = true; // Delete / Backspace
            if (vk == VK_ESCAPE)                              intercept = true; // Escape

            if (intercept) {
                SendMessageW(hwnd, WM_KEYDOWN, msg.wParam, msg.lParam);
                continue; // skip normal dispatch; child never sees this key
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnloadBundledFonts();
    return static_cast<int>(msg.wParam);
}
