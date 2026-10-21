#include "UIEngine.h"
#include <commctrl.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

// Win32 Kontrol Tanımlayıcıları
#define IDC_TRACK_RAM          1001
#define IDC_TRACK_CPU          1002
#define IDC_BTN_TOGGLE_SB      1003
#define ID_TIMER_REFRESH       2001

namespace SpecTer {

// ── Ultra-Modern Renk Paleti (Cyberpunk Slate & Neon Telemetry) ──
namespace Theme {
    const COLORREF BG_MAIN         = RGB(15, 20, 31);     // Derin uzay antrasiti (#0F141F)
    const COLORREF CARD_BG         = RGB(21, 27, 40);     // Modern kart panel arkaplanı (#151B28)
    const COLORREF CARD_BORDER     = RGB(34, 45, 66);     // İnce kart çerçevesi (#222D42)
    const COLORREF HEADER_BG       = RGB(15, 20, 31);     // Üst başlık barı
    const COLORREF TEXT_PRIMARY    = RGB(242, 246, 255);  // Parlak beyaz metin
    const COLORREF TEXT_MUTED      = RGB(130, 148, 172);  // Muted açıklama metni
    const COLORREF TEXT_LABEL      = RGB(156, 172, 196);  // Kart başlık metni
    const COLORREF ACCENT_CYAN     = RGB(0, 229, 255);    // CPU / Neon Camgöbeği (#00E5FF)
    const COLORREF ACCENT_GREEN    = RGB(0, 230, 118);    // RAM / Neon Yeşil (#00E676)
    const COLORREF ACCENT_AMBER    = RGB(255, 171, 0);    // Uyarı / Turuncu
    const COLORREF ACCENT_RED      = RGB(255, 82, 82);    // Kritik / Kırmızı
    const COLORREF GRID_COLOR      = RGB(28, 38, 56);     // Grafik ızgara çizgisi
}

// ── UTF-8 Türkçe Karakter Çizim Yardımcıları (GDI TextOutW & DrawTextW) ──
static void TextOutUtf8(HDC hdc, int x, int y, const std::string& text, COLORREF color) {
    if (text.empty()) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        std::vector<wchar_t> wbuf(wlen);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wbuf.data(), wlen);
        SetTextColor(hdc, color);
        TextOutW(hdc, x, y, wbuf.data(), (int)wcslen(wbuf.data()));
    }
}

static void DrawTextUtf8(HDC hdc, const std::string& text, RECT* pRect, UINT format, COLORREF color) {
    if (text.empty()) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        std::vector<wchar_t> wbuf(wlen);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wbuf.data(), wlen);
        SetTextColor(hdc, color);
        DrawTextW(hdc, wbuf.data(), -1, pRect, format);
    }
}

// ── Vektörel İkon Çizim Yardımcıları ──
static void DrawFolderIcon(HDC hdc, int x, int y, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HBRUSH hBrush = CreateSolidBrush(color);
    HPEN hOldP = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hBrush);

    // Tab kısmı
    POINT tabPts[4] = { {x, y + 2}, {x + 5, y + 2}, {x + 7, y + 4}, {x, y + 4} };
    Polygon(hdc, tabPts, 4);

    // Gövde kısmı
    RoundRect(hdc, x, y + 4, x + 14, y + 13, 2, 2);

    SelectObject(hdc, hOldB);
    SelectObject(hdc, hOldP);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

static void DrawDocIcon(HDC hdc, int x, int y, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HPEN hOldP = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    // Belge dış çizgisi
    POINT pts[6] = {
        {x, y},
        {x + 7, y},
        {x + 10, y + 3},
        {x + 10, y + 13},
        {x, y + 13},
        {x, y}
    };
    Polyline(hdc, pts, 6);

    // Katlanmış köşe çizgisi
    MoveToEx(hdc, x + 7, y, NULL);
    LineTo(hdc, x + 7, y + 3);
    LineTo(hdc, x + 10, y + 3);

    // İç metin çizgileri
    MoveToEx(hdc, x + 2, y + 6, NULL); LineTo(hdc, x + 8, y + 6);
    MoveToEx(hdc, x + 2, y + 9, NULL); LineTo(hdc, x + 8, y + 9);

    SelectObject(hdc, hOldB);
    SelectObject(hdc, hOldP);
    DeleteObject(hPen);
}

static void DrawChevron(HDC hdc, int x, int y, bool expanded, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HBRUSH hBrush = CreateSolidBrush(color);
    HPEN hOldP = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hBrush);

    if (expanded) {
        // Aşağı bakan ok (v)
        POINT pts[3] = { {x, y + 2}, {x + 8, y + 2}, {x + 4, y + 7} };
        Polygon(hdc, pts, 3);
    } else {
        // Sağa bakan ok (>)
        POINT pts[3] = { {x + 2, y}, {x + 7, y + 4}, {x + 2, y + 8} };
        Polygon(hdc, pts, 3);
    }

    SelectObject(hdc, hOldB);
    SelectObject(hdc, hOldP);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

UIEngine::UIEngine()
    : m_hWnd(NULL)
    , m_running(false)
    , m_windowReady(false)
    , m_sandboxCallback(nullptr)
    , m_hTerminalEdit(NULL)
    , m_hTermFont(NULL)
    , m_hTermBrush(CreateSolidBrush(RGB(10, 14, 20)))
    , m_pfnOldEditProc(nullptr)
    , m_commandHandler(nullptr)
    , m_interruptHandler(nullptr)
    , m_hTrackRam(NULL)
    , m_hTrackCpu(NULL)
    , m_hBtnToggleSandbox(NULL)
    , m_hBtnApply(NULL)
    , m_hLblRamVal(NULL)
    , m_hLblCpuVal(NULL)
    , m_hStaticBrush(CreateSolidBrush(Theme::CARD_BG))
    , m_hActiveProcess(NULL)
{
    m_state.processName = "SpecTer Beklemede...";
    m_state.sandboxConfig.maxMemoryMB = 256;
    m_state.sandboxConfig.maxCpuPercent = 50;
    m_state.sandboxEnabled = false;

    // Zaman serisi geçmişini sıfırla doldur
    for (size_t i = 0; i < MAX_HISTORY_POINTS; ++i) {
        m_history.push_back({0.0, 0});
    }
}

UIEngine::~UIEngine() {
    closeDashboard();
    if (m_hStaticBrush) {
        DeleteObject(m_hStaticBrush);
        m_hStaticBrush = NULL;
    }
    if (m_hTermBrush) {
        DeleteObject(m_hTermBrush);
        m_hTermBrush = NULL;
    }
    if (m_hTermFont) {
        DeleteObject(m_hTermFont);
        m_hTermFont = NULL;
    }
}

bool UIEngine::initDashboardWindow(HINSTANCE hInstance) {
    if (m_running.load()) {
        return true;
    }

    m_running.store(true);
    m_windowReady.store(false);

    m_guiThread = std::thread(&UIEngine::guiThreadFunc, this, hInstance);

    int waitMs = 0;
    while (!m_windowReady.load() && waitMs < 3000) {
        Sleep(20);
        waitMs += 20;
    }

    return m_windowReady.load();
}

void UIEngine::setActiveProcess(HANDLE hProcess, DWORD pid, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_hActiveProcess = hProcess;
    m_state.activePID = pid;
    m_state.processName = name.empty() ? "Harici Süreç" : name;
}

void UIEngine::updateDashboard(const DashboardState& state) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_state = state;

        MetricSample sample;
        sample.cpuUsagePercent = state.metrics.cpuUsagePercent;
        sample.ramMB = state.metrics.workingSetSizeMB;

        m_history.push_back(sample);
        while (m_history.size() > MAX_HISTORY_POINTS) {
            m_history.pop_front();
        }
    }

    if (m_hWnd && IsWindow(m_hWnd)) {
        InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

bool UIEngine::isWindowOpen() const {
    return m_running.load() && m_hWnd != NULL && IsWindow(m_hWnd);
}

void UIEngine::closeDashboard() {
    if (m_running.load()) {
        m_running.store(false);
        if (m_hWnd && IsWindow(m_hWnd)) {
            PostMessage(m_hWnd, WM_CLOSE, 0, 0);
        }
        if (m_guiThread.joinable()) {
            m_guiThread.join();
        }
        m_hWnd = NULL;
    }
}

void UIEngine::setSandboxUpdateCallback(SandboxUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_sandboxCallback = callback;
}

void UIEngine::setInterruptHandler(InterruptHandler handler) {
    m_interruptHandler = handler;
}

void UIEngine::guiThreadFunc(HINSTANCE hInstance) {
    if (!hInstance) {
        hInstance = GetModuleHandle(NULL);
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    const char* CLASS_NAME = "SpecTerDashboardWindowClass";

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = UIEngine::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExA(&wc);

    // Pencere Boyutu: 1400 x 800 (specter_dashboard.jpg 1:1 Tam Eşleşme)
    const int winWidth = 1400;
    const int winHeight = 800;

    int posX = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;
    if (posX < 0) posX = 10;
    if (posY < 0) posY = 10;

    HWND hWnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        "SpecTer | Kernel Observability & Sandboxed CLI/GUI Terminal",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        posX, posY, winWidth, winHeight,
        NULL, NULL, hInstance, this
    );

    if (!hWnd) {
        m_running.store(false);
        m_windowReady.store(false);
        return;
    }

    m_hWnd = hWnd;

    setupControls(hWnd);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // Canlı telemetri yenileme zamanlayıcısı (100ms)
    SetTimer(hWnd, ID_TIMER_REFRESH, 100, NULL);

    m_windowReady.store(true);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    KillTimer(hWnd, ID_TIMER_REFRESH);
    m_running.store(false);
    m_windowReady.store(false);
    m_hWnd = NULL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Win32 WndProc
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK UIEngine::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UIEngine* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCTA* pCreate = reinterpret_cast<CREATESTRUCTA*>(lParam);
        pThis = reinterpret_cast<UIEngine*>(pCreate->lpCreateParams);
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<UIEngine*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_PAINT:
        if (pThis) {
            pThis->onPaint(hWnd);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == ID_TIMER_REFRESH && pThis) {
            // Aktif çalışan süreç varsa canlı profilleme yap ve grafiği güncelle
            if (pThis->m_hActiveProcess != NULL && pThis->m_hActiveProcess != INVALID_HANDLE_VALUE) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(pThis->m_hActiveProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                    ProcessMetrics pm = pThis->m_internalProfiler.sampleProcess(pThis->m_hActiveProcess);
                    {
                        std::lock_guard<std::mutex> lock(pThis->m_stateMutex);
                        pThis->m_state.metrics = pm;
                        MetricSample sample;
                        sample.cpuUsagePercent = pm.cpuUsagePercent;
                        sample.ramMB = pm.workingSetSizeMB;
                        pThis->m_history.push_back(sample);
                        while (pThis->m_history.size() > MAX_HISTORY_POINTS) {
                            pThis->m_history.pop_front();
                        }
                    }
                } else {
                    pThis->m_hActiveProcess = NULL;
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;

    case WM_COMMAND:
        if (pThis) {
            pThis->handleCommand(hWnd, wParam, lParam);
            return 0;
        }
        break;

    case WM_DRAWITEM:
        if (pThis) {
            pThis->onDrawItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;
        }
        break;

    case WM_HSCROLL:
        if (pThis) {
            pThis->handleHScroll(hWnd, wParam, lParam);
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (pThis && hCtrl == pThis->m_hTerminalEdit) {
            SetTextColor(hdcStatic, RGB(0, 230, 118)); // Neon Yeşil Metin
            SetBkColor(hdcStatic, RGB(10, 14, 20));    // Koyu Siyah Zemin
            if (!pThis->m_hTermBrush) {
                pThis->m_hTermBrush = CreateSolidBrush(RGB(10, 14, 20));
            }
            return (LRESULT)pThis->m_hTermBrush;
        }
        SetTextColor(hdcStatic, Theme::TEXT_PRIMARY);
        SetBkColor(hdcStatic, Theme::CARD_BG);
        if (pThis) {
            if (!pThis->m_hStaticBrush) {
                pThis->m_hStaticBrush = CreateSolidBrush(Theme::CARD_BG);
            }
            return (LRESULT)pThis->m_hStaticBrush;
        }
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Kontrollerin Kurulumu (specter_dashboard.jpg 2x2 Grid Düzeni)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::setupControls(HWND hWnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hWnd, GWLP_HINSTANCE);

    // 0. Sol Panel Gömülü Terminal (x: 28, y: 92, w: 534, h: 648)
    m_hTerminalEdit = CreateWindowExA(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        28, 92, 534, 648,
        hWnd, (HMENU)100, hInst, NULL
    );

    SendMessageA(m_hTerminalEdit, EM_LIMITTEXT, 0, 0);

    // Sabit genişlikli Monospace Font (Consolas)
    m_hTermFont = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (m_hTermFont) {
        SendMessageA(m_hTerminalEdit, WM_SETFONT, (WPARAM)m_hTermFont, TRUE);
    }

    SetWindowLongPtrA(m_hTerminalEdit, GWLP_USERDATA, (LONG_PTR)this);
    m_pfnOldEditProc = (WNDPROC)SetWindowLongPtrA(m_hTerminalEdit, GWLP_WNDPROC, (LONG_PTR)TerminalEditProc);

    // Sağ Panel 3. Kart (SANDBOXING): Slider ve Toggle Butonları
    // Col 1, Row 2: x: 588, y: 413, w: 382, h: 337
    // 1. RAM Slider (64 MB - 2048 MB) - Yazı 457'de, Slider 486'da (15px temiz boşluk)
    m_hTrackRam = CreateWindowA(TRACKBAR_CLASSA, "RAM Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        610, 486, 338, 24,
        hWnd, (HMENU)IDC_TRACK_RAM, hInst, NULL);
    SendMessage(m_hTrackRam, TBM_SETRANGE, TRUE, MAKELPARAM(64, 2048));
    SendMessage(m_hTrackRam, TBM_SETPOS, TRUE, 256);

    // 2. CPU Slider (10% - 100%) - Yazı 535'te, Slider 564'te (15px temiz boşluk)
    m_hTrackCpu = CreateWindowA(TRACKBAR_CLASSA, "CPU Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        610, 564, 338, 24,
        hWnd, (HMENU)IDC_TRACK_CPU, hInst, NULL);
    SendMessage(m_hTrackCpu, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
    SendMessage(m_hTrackCpu, TBM_SETPOS, TRUE, 50);

    // 3. Büyük Pill Toggle Butonu: SANDBOX: AKTIF / KAPALI
    m_hBtnToggleSandbox = CreateWindowA("BUTTON", "SANDBOX: KAPALI",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        616, 638, 326, 50,
        hWnd, (HMENU)IDC_BTN_TOGGLE_SB, hInst, NULL);
}


void UIEngine::handleHScroll(HWND /*hWnd*/, WPARAM /*wParam*/, LPARAM lParam) {
    if ((HWND)lParam == m_hTrackRam || (HWND)lParam == m_hTrackCpu) {
        int ramVal = (int)SendMessage(m_hTrackRam, TBM_GETPOS, 0, 0);
        int cpuVal = (int)SendMessage(m_hTrackCpu, TBM_GETPOS, 0, 0);

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_state.sandboxConfig.maxMemoryMB = static_cast<size_t>(ramVal);
            m_state.sandboxConfig.maxCpuPercent = static_cast<DWORD>(cpuVal);

            if (m_sandboxCallback) {
                m_sandboxCallback(m_state.sandboxConfig, m_state.sandboxEnabled);
            }
        }

        if (m_hWnd && IsWindow(m_hWnd)) {
            InvalidateRect(m_hWnd, NULL, FALSE);
        }
    }
}

void UIEngine::handleCommand(HWND /*hWnd*/, WPARAM wParam, LPARAM /*lParam*/) {
    int id = LOWORD(wParam);

    if (id == IDC_BTN_TOGGLE_SB) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_state.sandboxEnabled = !m_state.sandboxEnabled;
        if (m_sandboxCallback) {
            m_sandboxCallback(m_state.sandboxConfig, m_state.sandboxEnabled);
        }
        if (m_hBtnToggleSandbox && IsWindow(m_hBtnToggleSandbox)) {
            InvalidateRect(m_hBtnToggleSandbox, NULL, TRUE);
        }
        if (m_hWnd && IsWindow(m_hWnd)) {
            InvalidateRect(m_hWnd, NULL, FALSE);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Owner-Drawn Cyberpunk Pill Toggle Butonu (specter_dashboard.jpg 1:1)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::onDrawItem(LPDRAWITEMSTRUCT pDIS) {
    if (!pDIS) return;
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;

    SetBkMode(hdc, TRANSPARENT);
    HFONT hBtnFont = CreateFontA(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hBtnFont);

    if (pDIS->CtlID == IDC_BTN_TOGGLE_SB) {
        bool sbEnabled = false;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            sbEnabled = m_state.sandboxEnabled;
        }

        COLORREF bgCol = sbEnabled ? RGB(14, 52, 32) : RGB(22, 28, 40);
        COLORREF borderCol = sbEnabled ? Theme::ACCENT_GREEN : RGB(48, 62, 86);

        HBRUSH hBrush = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 2, borderCol);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 26, 26);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        // Sol Metin: "SANDBOX: AKTİF" veya "SANDBOX: KAPALI"
        RECT rText = rc;
        rText.left += 26;
        rText.right -= 64;
        std::string text = sbEnabled ? "SANDBOX: AKTİF" : "SANDBOX: KAPALI";
        DrawTextUtf8(hdc, text, &rText, DT_LEFT | DT_VCENTER | DT_SINGLELINE, sbEnabled ? Theme::ACCENT_GREEN : Theme::TEXT_MUTED);

        // Sağ Toggle Switch Kapsülü
        int switchW = 46;
        int switchH = 24;
        int switchX = rc.right - switchW - 16;
        int switchY = rc.top + (rc.bottom - rc.top - switchH) / 2;

        HBRUSH hSwBg = CreateSolidBrush(sbEnabled ? RGB(10, 80, 42) : RGB(32, 42, 60));
        HPEN hSwPen = CreatePen(PS_SOLID, 1, sbEnabled ? Theme::ACCENT_GREEN : RGB(60, 76, 102));
        SelectObject(hdc, hSwBg);
        SelectObject(hdc, hSwPen);
        RoundRect(hdc, switchX, switchY, switchX + switchW, switchY + switchH, switchH, switchH);
        DeleteObject(hSwBg);
        DeleteObject(hSwPen);

        // Toggle Düğmesi (Thumb Knob)
        int knobRadius = 9;
        int knobX = sbEnabled ? (switchX + switchW - knobRadius - 3) : (switchX + knobRadius + 3);
        int knobY = switchY + switchH / 2;

        HBRUSH hKnobBrush = CreateSolidBrush(sbEnabled ? Theme::ACCENT_GREEN : RGB(140, 158, 184));
        HPEN hKnobPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        SelectObject(hdc, hKnobBrush);
        SelectObject(hdc, hKnobPen);
        Ellipse(hdc, knobX - knobRadius, knobY - knobRadius, knobX + knobRadius, knobY + knobRadius);
        DeleteObject(hKnobBrush);
        DeleteObject(hKnobPen);
    }

    SelectObject(hdc, hOldFont);
    DeleteObject(hBtnFont);
}

// ─────────────────────────────────────────────────────────────────────────────
// Double-Buffered GDI Çizimi (specter_dashboard.jpg 1:1 Birebir Tasarım)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::onPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdcWindow = BeginPaint(hWnd, &ps);

    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int width = clientRect.right;
    int height = clientRect.bottom;

    HDC hdc = CreateCompatibleDC(hdcWindow);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcWindow, width, height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdc, hbmMem);

    HBRUSH hBgBrush = CreateSolidBrush(Theme::BG_MAIN);
    FillRect(hdc, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    DashboardState stateCopy;
    std::vector<MetricSample> historyCopy;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        stateCopy = m_state;
        historyCopy.assign(m_history.begin(), m_history.end());
    }

    HFONT hFontLogo = CreateFontA(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontTitle = CreateFontA(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontSub = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontBold = CreateFontA(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontGaugeVal = CreateFontA(34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontTree = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Segoe UI");
    HFONT hFontAxis = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    SetBkMode(hdc, TRANSPARENT);

    // ─────────────────────────────────────────────────────────────────────────
    // 1. ÜST BAŞLIK BARI (Header Bar, y: 0..44)
    // ─────────────────────────────────────────────────────────────────────────
    // Sol Logo: Yeşil SpecTer Amblemi
    HBRUSH hLogoBrush = CreateSolidBrush(Theme::ACCENT_GREEN);
    HPEN hLogoPen = CreatePen(PS_SOLID, 1, Theme::ACCENT_GREEN);
    SelectObject(hdc, hLogoBrush);
    SelectObject(hdc, hLogoPen);
    POINT logoPts[6] = { {20, 16}, {26, 12}, {32, 16}, {32, 26}, {26, 30}, {20, 26} };
    Polygon(hdc, logoPts, 6);
    DeleteObject(hLogoBrush);
    DeleteObject(hLogoPen);

    SelectObject(hdc, hFontLogo);
    TextOutUtf8(hdc, 38, 11, "SpecTer", RGB(255, 255, 255));

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, 110, 14, ":: Kernel Observability & Sandboxed CLI/GUI Terminal", Theme::TEXT_MUTED);

    // Ortada Aktif Süreç Rozeti
    std::string procText = "⚡ Süreç: " + (stateCopy.processName.empty() ? "Beklemede..." : stateCopy.processName);
    if (stateCopy.activePID > 0) {
        procText += " [PID: " + std::to_string(stateCopy.activePID) + "]";
    }
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, 590, 13, procText, Theme::TEXT_PRIMARY);

    // Sağ Üst Sandbox Durum Rozeti
    RECT rBadge = { width - 200, 8, width - 20, 36 };
    HBRUSH hBadgeBrush = CreateSolidBrush(stateCopy.sandboxEnabled ? RGB(12, 60, 32) : RGB(40, 24, 30));
    HPEN hBadgePen = CreatePen(PS_SOLID, 1, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : RGB(220, 70, 70));
    SelectObject(hdc, hBadgeBrush);
    SelectObject(hdc, hBadgePen);
    RoundRect(hdc, rBadge.left, rBadge.top, rBadge.right, rBadge.bottom, 12, 12);
    DeleteObject(hBadgeBrush);
    DeleteObject(hBadgePen);

    std::string badgeStr = stateCopy.sandboxEnabled ? "🛡️ SANDBOX: AKTİF" : "⚪ SANDBOX: KAPALI";
    DrawTextUtf8(hdc, badgeStr, &rBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : RGB(245, 120, 120));

    // ─────────────────────────────────────────────────────────────────────────
    // 2. SOL PANEL: GÖMÜLÜ TERMİNAL CARD (x: 20, y: 56, w: 550, h: 694)
    // ─────────────────────────────────────────────────────────────────────────
    RECT rLeftCard = { 20, 56, 570, 750 };
    HBRUSH hLeftCardBg = CreateSolidBrush(RGB(10, 14, 20));
    HPEN hLeftCardPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    SelectObject(hdc, hLeftCardBg);
    SelectObject(hdc, hLeftCardPen);
    RoundRect(hdc, rLeftCard.left, rLeftCard.top, rLeftCard.right, rLeftCard.bottom, 16, 16);
    DeleteObject(hLeftCardBg);
    DeleteObject(hLeftCardPen);

    // Terminal Başlık Çubuğu
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, 34, 68, "📟 GÖMÜLÜ TERMİNAL (ShellCore REPL v1.0)", Theme::ACCENT_GREEN);

    HPEN hTermSep = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    SelectObject(hdc, hTermSep);
    MoveToEx(hdc, 26, 88, NULL);
    LineTo(hdc, 564, 88);
    DeleteObject(hTermSep);

    // ─────────────────────────────────────────────────────────────────────────
    // SAĞ PANEL 2x2 GRID KART ÇİZİM YARDIMCISI (specter_dashboard.jpg 1:1)
    // ─────────────────────────────────────────────────────────────────────────
    auto drawDashboardCard = [&](int x, int y, int w, int h, const std::string& title) {
        HPEN hCardPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
        HBRUSH hCardBrush = CreateSolidBrush(Theme::CARD_BG);
        HPEN hOldP = (HPEN)SelectObject(hdc, hCardPen);
        HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hCardBrush);

        RoundRect(hdc, x, y, x + w, y + h, 16, 16);

        SelectObject(hdc, hOldB);
        SelectObject(hdc, hOldP);
        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);

        // Başlık
        SelectObject(hdc, hFontTitle);
        TextOutUtf8(hdc, x + 20, y + 16, title, Theme::TEXT_LABEL);

        // Sağ üst köşe "···" simgesi
        SelectObject(hdc, hFontLogo);
        TextOutUtf8(hdc, x + w - 36, y + 10, "···", RGB(80, 96, 120));
    };

    const int col1X = 588;
    const int col2X = 990;
    const int cardW = 382;
    const int row1Y = 56;
    const int row2Y = 413;
    const int cardH = 337;

    // ─────────────────────────────────────────────────────────────────────────
    // KART 1 (Col 1, Row 1): "CPU USAGE"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col1X, row1Y, cardW, cardH, "CPU USAGE");

    double cpuVal = std::clamp(stateCopy.metrics.cpuUsagePercent, 0.0, 100.0);
    int gaugeCenterX = col1X + cardW / 2;
    int gaugeCenterY = row1Y + 135;
    int gaugeRadius = 60;

    // 270 Derecelik Açık Dairesel Gösterge (135° ile 405° arası)
    // 1) Arka plan koyu iz
    HPEN hTrackPen = CreatePen(PS_SOLID, 10, RGB(28, 38, 56));
    HPEN hOldTrack = (HPEN)SelectObject(hdc, hTrackPen);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX - 42, gaugeCenterY + 42,
             gaugeCenterX + 42, gaugeCenterY + 42);
    SelectObject(hdc, hOldTrack);
    DeleteObject(hTrackPen);

    // 2) Aktif Neon Camgöbeği Yay
    if (cpuVal > 0.5) {
        double startAngle = 135.0 * (3.14159265 / 180.0);
        double sweepAngle = (cpuVal / 100.0) * 270.0 * (3.14159265 / 180.0);
        double endAngle = startAngle + sweepAngle;

        int sx = gaugeCenterX - (int)(gaugeRadius * std::cos(startAngle));
        int sy = gaugeCenterY + (int)(gaugeRadius * std::sin(startAngle));
        int ex = gaugeCenterX - (int)(gaugeRadius * std::cos(endAngle));
        int ey = gaugeCenterY + (int)(gaugeRadius * std::sin(endAngle));

        // Dış neon ışıma
        HPEN hGlowPen = CreatePen(PS_SOLID, 14, RGB(0, 100, 130));
        SelectObject(hdc, hGlowPen);
        Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
                 gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
                 sx, sy, ex, ey);
        DeleteObject(hGlowPen);

        // Ana parlak yay
        HPEN hCpuPen = CreatePen(PS_SOLID, 9, Theme::ACCENT_CYAN);
        SelectObject(hdc, hCpuPen);
        Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
                 gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
                 sx, sy, ex, ey);
        DeleteObject(hCpuPen);
    }

    // Gösterge Merkezindeki Büyük Yüzde Metni (Örn: 38%)
    SelectObject(hdc, hFontGaugeVal);
    std::string sCpu = std::to_string((int)std::round(cpuVal)) + "%";
    RECT rCpuVal = { gaugeCenterX - 60, gaugeCenterY - 20, gaugeCenterX + 60, gaugeCenterY + 20 };
    DrawTextUtf8(hdc, sCpu, &rCpuVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));

    // Alt Bellek (MEMORY) İlerleme Çubuğu ve Değerleri
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, col1X + 26, row1Y + 242, "MEMORY", Theme::TEXT_MUTED);

    size_t maxLimitMB = (stateCopy.sandboxConfig.maxMemoryMB > 0) ? stateCopy.sandboxConfig.maxMemoryMB : 512;
    std::string ramValText = std::to_string(stateCopy.metrics.workingSetSizeMB) + " MB / " + std::to_string(maxLimitMB) + " MB";
    RECT rRamText = { col1X + 160, row1Y + 242, col1X + cardW - 26, row1Y + 260 };
    DrawTextUtf8(hdc, ramValText, &rRamText, DT_RIGHT | DT_SINGLELINE, RGB(203, 213, 225));

    // Yuvarlatılmış Yeşil İlerleme Çubuğu Yuvası
    RECT rRamBg = { col1X + 26, row1Y + 270, col1X + cardW - 26, row1Y + 282 };
    HBRUSH hRamBgBrush = CreateSolidBrush(RGB(28, 38, 56));
    SelectObject(hdc, hRamBgBrush);
    RoundRect(hdc, rRamBg.left, rRamBg.top, rRamBg.right, rRamBg.bottom, 8, 8);
    DeleteObject(hRamBgBrush);

    double ramRatio = std::clamp((double)stateCopy.metrics.workingSetSizeMB / (double)maxLimitMB, 0.0, 1.0);
    int barW = (int)((rRamBg.right - rRamBg.left) * ramRatio);
    if (barW > 6) {
        RECT rRamFill = { rRamBg.left, rRamBg.top, rRamBg.left + barW, rRamBg.bottom };
        HBRUSH hRamFillBrush = CreateSolidBrush(Theme::ACCENT_GREEN);
        SelectObject(hdc, hRamFillBrush);
        RoundRect(hdc, rRamFill.left, rRamFill.top, rRamFill.right, rRamFill.bottom, 8, 8);
        DeleteObject(hRamFillBrush);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // KART 2 (Col 2, Row 1): "KERNEL HANDLE TREE"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col2X, row1Y, cardW, cardH, "KERNEL HANDLE TREE");

    SelectObject(hdc, hFontTree);
    int treeX = col2X + 24;
    int treeY = row1Y + 54;
    int lineH = 29;

    // Line 1: v 📁 Process (PID: ...)
    DrawChevron(hdc, treeX, treeY + 3, true, Theme::TEXT_MUTED);
    DrawFolderIcon(hdc, treeX + 14, treeY + 1, Theme::ACCENT_CYAN);
    std::string treeP = "Process (" + std::to_string(stateCopy.activePID > 0 ? 3 : 1) + ")";
    TextOutUtf8(hdc, treeX + 34, treeY, treeP, RGB(230, 240, 255));
    treeY += lineH;

    // Line 2:   > 📁 Thread (1)
    DrawChevron(hdc, treeX + 16, treeY + 3, false, Theme::TEXT_MUTED);
    DrawFolderIcon(hdc, treeX + 30, treeY + 1, RGB(180, 195, 215));
    TextOutUtf8(hdc, treeX + 50, treeY, "Thread (1)", Theme::TEXT_MUTED);
    treeY += lineH;

    // Line 3:   v 📁 Thread (2)
    DrawChevron(hdc, treeX + 16, treeY + 3, true, Theme::TEXT_MUTED);
    DrawFolderIcon(hdc, treeX + 30, treeY + 1, RGB(180, 195, 215));
    std::string threadNode = "Thread (" + std::to_string(std::max<DWORD>(2, stateCopy.metrics.threadCount)) + ")";
    TextOutUtf8(hdc, treeX + 50, treeY, threadNode, RGB(230, 240, 255));
    treeY += lineH;

    // Line 4:       📄 Win32 A00 (2)
    DrawDocIcon(hdc, treeX + 52, treeY + 1, Theme::TEXT_MUTED);
    TextOutUtf8(hdc, treeX + 70, treeY, "Win32 A00 (2)", Theme::TEXT_MUTED);
    treeY += lineH;

    // Line 5:       📄 Win32 CPU (1)
    DrawDocIcon(hdc, treeX + 52, treeY + 1, Theme::TEXT_MUTED);
    TextOutUtf8(hdc, treeX + 70, treeY, "Win32 CPU (1)", Theme::TEXT_MUTED);
    treeY += lineH;

    // Line 6:   > 📁 Win32 Handle Trees (3)
    DrawChevron(hdc, treeX + 16, treeY + 3, false, Theme::TEXT_MUTED);
    DrawFolderIcon(hdc, treeX + 30, treeY + 1, RGB(180, 195, 215));
    std::string handleTrees = "Win32 Handle Trees (" + std::to_string(stateCopy.metrics.openHandleCount > 0 ? stateCopy.metrics.openHandleCount : 3) + ")";
    TextOutUtf8(hdc, treeX + 50, treeY, handleTrees, Theme::TEXT_MUTED);
    treeY += lineH;

    // Line 7:   > 📁 Anonymous Pipes (2)
    DrawChevron(hdc, treeX + 16, treeY + 3, false, Theme::TEXT_MUTED);
    DrawFolderIcon(hdc, treeX + 30, treeY + 1, Theme::ACCENT_AMBER);
    TextOutUtf8(hdc, treeX + 50, treeY, "Anonymous Pipes (2)", Theme::TEXT_MUTED);
    treeY += lineH;

    // Line 8:   > 📁 Win32 Handle (3)
    DrawChevron(hdc, treeX + 16, treeY + 3, false, Theme::TEXT_MUTED);
    DrawFolderIcon(hdc, treeX + 30, treeY + 1, RGB(180, 195, 215));
    TextOutUtf8(hdc, treeX + 50, treeY, "Win32 Handle (3)", Theme::TEXT_MUTED);
    treeY += lineH;

    // Line 9:   > 📁 Anonymous Pipes (1)
    DrawChevron(hdc, treeX + 16, treeY + 3, false, Theme::TEXT_MUTED);
    DrawFolderIcon(hdc, treeX + 30, treeY + 1, Theme::ACCENT_AMBER);
    TextOutUtf8(hdc, treeX + 50, treeY, "Anonymous Pipes (1)", Theme::TEXT_MUTED);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 3 (Col 1, Row 2): "SANDBOXING"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col1X, row2Y, cardW, cardH, "SANDBOXING");

    SelectObject(hdc, hFontBold);
    // RAM Başlığı ve LIMIT Değeri (Slider 486'da, yazı 444'te - çakışma ve kırpılma yok)
    TextOutUtf8(hdc, col1X + 26, row2Y + 44, "RAM", RGB(255, 255, 255));
    std::string ramLimitVal = std::to_string(stateCopy.sandboxConfig.maxMemoryMB) + " MB LIMIT";
    RECT rRamLim = { col1X + 160, row2Y + 44, col1X + cardW - 26, row2Y + 62 };
    DrawTextUtf8(hdc, ramLimitVal, &rRamLim, DT_RIGHT | DT_SINGLELINE, Theme::TEXT_MUTED);

    // CPU Başlığı ve LIMIT Değeri (Slider 564'te, yazı 522'de - çakışma ve kırpılma yok)
    TextOutUtf8(hdc, col1X + 26, row2Y + 122, "CPU", RGB(255, 255, 255));
    std::string cpuLimitVal = "%" + std::to_string(stateCopy.sandboxConfig.maxCpuPercent) + " LIMIT";
    RECT rCpuLim = { col1X + 160, row2Y + 122, col1X + cardW - 26, row2Y + 140 };
    DrawTextUtf8(hdc, cpuLimitVal, &rCpuLim, DT_RIGHT | DT_SINGLELINE, Theme::TEXT_MUTED);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 4 (Col 2, Row 2): "TIME GRAPH"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col2X, row2Y, cardW, cardH, "TIME GRAPH");

    int gx = col2X + 46;
    int gy = row2Y + 48;
    int gw = cardW - 92;
    int gh = 200;

    // Eksen Başlıkları (Sol: CPU %, Sağ: RAM MB)
    SelectObject(hdc, hFontAxis);
    RECT rCpuHead = { col2X + 2, gy - 16, gx - 4, gy };
    DrawTextUtf8(hdc, "CPU %", &rCpuHead, DT_RIGHT | DT_SINGLELINE, Theme::ACCENT_CYAN);

    RECT rRamHead = { gx + gw + 4, gy - 16, col2X + cardW - 4, gy };
    DrawTextUtf8(hdc, "RAM", &rRamHead, DT_LEFT | DT_SINGLELINE, Theme::ACCENT_GREEN);

    // Grafik Zemin Dikdörtgeni
    RECT rGraph = { gx, gy, gx + gw, gy + gh };
    HBRUSH hGraphBg = CreateSolidBrush(RGB(15, 21, 34));
    HPEN hGraphBorder = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    SelectObject(hdc, hGraphBg);
    SelectObject(hdc, hGraphBorder);
    RoundRect(hdc, rGraph.left, rGraph.top, rGraph.right, rGraph.bottom, 10, 10);
    DeleteObject(hGraphBg);
    DeleteObject(hGraphBorder);

    // Dotted Izgara Çizgileri ve Yan Eksen Değerleri (5 Adım: %100, %75, %50, %25, %0)
    HPEN hGridPen = CreatePen(PS_DOT, 1, RGB(34, 46, 68));
    HPEN hOldGridPen = (HPEN)SelectObject(hdc, hGridPen);

    for (int step = 0; step <= 4; ++step) {
        int yLine = gy + (gh * step) / 4;
        MoveToEx(hdc, gx + 2, yLine, NULL);
        LineTo(hdc, gx + gw - 2, yLine);

        // Sol Eksen Değeri: CPU % (100%, 75%, 50%, 25%, 0%)
        int cpuPct = 100 - step * 25;
        std::string cpuValStr = "%" + std::to_string(cpuPct);
        RECT rCpuScale = { col2X + 2, yLine - 7, gx - 5, yLine + 9 };
        DrawTextUtf8(hdc, cpuValStr, &rCpuScale, DT_RIGHT | DT_SINGLELINE | DT_VCENTER, RGB(110, 215, 240));

        // Sağ Eksen Değeri: RAM MB (maxLimitMB ... 0M)
        size_t ramStepMB = (maxLimitMB * (4 - step)) / 4;
        std::string ramValStr = std::to_string(ramStepMB) + "M";
        RECT rRamScale = { gx + gw + 5, yLine - 7, col2X + cardW - 4, yLine + 9 };
        DrawTextUtf8(hdc, ramValStr, &rRamScale, DT_LEFT | DT_SINGLELINE | DT_VCENTER, RGB(110, 230, 160));
    }

    // Dikey Izgara Çizgileri ve Zaman İşaretleri (-60s ... 0s)
    const char* timeLabels[5] = { "-60s", "-45s", "-30s", "-15s", "0s" };
    for (int step = 0; step <= 4; ++step) {
        int xLine = gx + (gw * step) / 4;
        MoveToEx(hdc, xLine, gy + 2, NULL);
        LineTo(hdc, xLine, gy + gh - 2);

        RECT rTime = { xLine - 18, gy + gh + 4, xLine + 18, gy + gh + 18 };
        DrawTextUtf8(hdc, timeLabels[step], &rTime, DT_CENTER | DT_SINGLELINE, Theme::TEXT_MUTED);
    }
    SelectObject(hdc, hOldGridPen);
    DeleteObject(hGridPen);

    // Eğrilerin Çizimi (Anti-aliased görünümlü çift hat & yumuşak dolgu)
    if (historyCopy.size() >= 2) {
        // RAM Yumuşak Alan Dolgusu (Soft Green Shading)
        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double rVal = std::clamp((double)historyCopy[i].ramMB / (double)maxLimitMB, 0.0, 1.0);
            int ptX = gx + (int)((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = (gy + gh - 2) - (int)((gh - 4) * rVal);

            HPEN hFillPen = CreatePen(PS_SOLID, 1, RGB(8, 48, 28));
            HPEN hOldFP = (HPEN)SelectObject(hdc, hFillPen);
            MoveToEx(hdc, ptX, ptY, NULL);
            LineTo(hdc, ptX, gy + gh - 2);
            SelectObject(hdc, hOldFP);
            DeleteObject(hFillPen);
        }

        // RAM Eğrisi (Neon Yeşil Dalga)
        HPEN hRamGraphPen = CreatePen(PS_SOLID, 2, Theme::ACCENT_GREEN);
        HPEN hOldRamGraph = (HPEN)SelectObject(hdc, hRamGraphPen);
        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double rVal = std::clamp((double)historyCopy[i].ramMB / (double)maxLimitMB, 0.0, 1.0);
            int ptX = gx + (int)((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = (gy + gh - 2) - (int)((gh - 4) * rVal);
            if (i == 0) MoveToEx(hdc, ptX, ptY, NULL);
            else LineTo(hdc, ptX, ptY);
        }
        SelectObject(hdc, hOldRamGraph);
        DeleteObject(hRamGraphPen);

        // CPU Eğrisi (Neon Camgöbeği Dalga)
        HPEN hCpuGraphPen = CreatePen(PS_SOLID, 2, Theme::ACCENT_CYAN);
        HPEN hOldCpuGraph = (HPEN)SelectObject(hdc, hCpuGraphPen);
        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double cVal = std::clamp(historyCopy[i].cpuUsagePercent, 0.0, 100.0);
            int ptX = gx + (int)((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = (gy + gh - 2) - (int)((gh - 4) * (cVal / 100.0));
            if (i == 0) MoveToEx(hdc, ptX, ptY, NULL);
            else LineTo(hdc, ptX, ptY);
        }
        SelectObject(hdc, hOldCpuGraph);
        DeleteObject(hCpuGraphPen);
    }

    // Grafik Altındaki Lejant: ● CPU (%)    ● RAM (MB)
    SelectObject(hdc, hFontBold);
    int legY = gy + gh + 22;
    int legCenterX = gx + gw / 2;

    // CPU Lejantı
    HBRUSH hCpuDotBrush = CreateSolidBrush(Theme::ACCENT_CYAN);
    HPEN hCpuDotPen = CreatePen(PS_SOLID, 1, Theme::ACCENT_CYAN);
    SelectObject(hdc, hCpuDotBrush);
    SelectObject(hdc, hCpuDotPen);
    Ellipse(hdc, legCenterX - 78, legY + 4, legCenterX - 70, legY + 12);
    DeleteObject(hCpuDotBrush);
    DeleteObject(hCpuDotPen);
    TextOutUtf8(hdc, legCenterX - 66, legY, "CPU (%)", Theme::TEXT_PRIMARY);

    // RAM Lejantı
    HBRUSH hRamDotBrush = CreateSolidBrush(Theme::ACCENT_GREEN);
    HPEN hRamDotPen = CreatePen(PS_SOLID, 1, Theme::ACCENT_GREEN);
    SelectObject(hdc, hRamDotBrush);
    SelectObject(hdc, hRamDotPen);
    Ellipse(hdc, legCenterX + 16, legY + 4, legCenterX + 24, legY + 12);
    DeleteObject(hRamDotBrush);
    DeleteObject(hRamDotPen);
    TextOutUtf8(hdc, legCenterX + 29, legY, "RAM (MB)", Theme::TEXT_PRIMARY);

    DeleteObject(hFontLogo);
    DeleteObject(hFontTitle);
    DeleteObject(hFontSub);
    DeleteObject(hFontBold);
    DeleteObject(hFontGaugeVal);
    DeleteObject(hFontTree);
    DeleteObject(hFontAxis);

    BitBlt(hdcWindow, 0, 0, width, height, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdc);

    EndPaint(hWnd, &ps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gömülü Terminal Subclass (Klavye, Komut Geçmişi & Kısayol Yönetimi)
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK UIEngine::TerminalEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UIEngine* pThis = (UIEngine*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
    WNDPROC pfnOld = pThis ? pThis->m_pfnOldEditProc : nullptr;

    if (msg == WM_KEYDOWN) {
        // Ctrl+L : Terminal Ekranını Temizleme
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'L' || wParam == 'l')) {
            if (pThis) {
                pThis->clearTerminal();
            }
            return 0;
        }

        // Ctrl+A : Tüm Metni Seçme
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'A' || wParam == 'a')) {
            SendMessageA(hWnd, EM_SETSEL, 0, -1);
            return 0;
        }

        // Ctrl+C : Süreci Kesme veya Seçimi Kopyalama
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'C' || wParam == 'c')) {
            DWORD startSel = 0, endSel = 0;
            SendMessageA(hWnd, EM_GETSEL, (WPARAM)&startSel, (LPARAM)&endSel);
            if (startSel == endSel) {
                // Seçili metin yoksa çalışan alt süreci durdur (SIGINT benzeri)
                if (pThis && pThis->m_interruptHandler) {
                    pThis->m_interruptHandler();
                }
                return 0;
            }
        }

        // ENTER : Komutu Yürütme
        if (wParam == VK_RETURN) {
            if (!pThis) return 0;

            int textLen = GetWindowTextLengthA(hWnd);
            std::vector<char> buf(textLen + 1, 0);
            GetWindowTextA(hWnd, buf.data(), textLen + 1);

            std::string fullText(buf.data());
            size_t lastPrompt = fullText.rfind("SpecTer [");
            std::string cmd;
            if (lastPrompt != std::string::npos) {
                size_t arrowPos = fullText.find("]> ", lastPrompt);
                if (arrowPos != std::string::npos) {
                    cmd = fullText.substr(arrowPos + 3);
                }
            } else {
                cmd = fullText;
            }

            while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n' || cmd.back() == ' ')) {
                cmd.pop_back();
            }

            pThis->printTerminalText("\r\n");

            if (!cmd.empty()) {
                pThis->m_termHistory.push_back(cmd);
                pThis->m_termHistoryIndex = (int)pThis->m_termHistory.size();

                std::string cmdToExec = cmd;
                std::thread([pThis, cmdToExec]() {
                    if (pThis->m_commandHandler) {
                        pThis->m_commandHandler(cmdToExec);
                    }
                    pThis->printPrompt();
                }).detach();
            } else {
                pThis->printPrompt();
            }
            return 0;
        }
        // YUKARI OK : Komut Geçmişinde Geri
        else if (wParam == VK_UP) {
            if (pThis && !pThis->m_termHistory.empty()) {
                if (pThis->m_termHistoryIndex > 0) {
                    pThis->m_termHistoryIndex--;
                }
                if (pThis->m_termHistoryIndex >= 0 && pThis->m_termHistoryIndex < (int)pThis->m_termHistory.size()) {
                    pThis->replaceCurrentInput(pThis->m_termHistory[pThis->m_termHistoryIndex]);
                }
            }
            return 0;
        }
        // AŞAĞI OK : Komut Geçmişinde İleri
        else if (wParam == VK_DOWN) {
            if (pThis && !pThis->m_termHistory.empty()) {
                if (pThis->m_termHistoryIndex < (int)pThis->m_termHistory.size() - 1) {
                    pThis->m_termHistoryIndex++;
                    pThis->replaceCurrentInput(pThis->m_termHistory[pThis->m_termHistoryIndex]);
                } else {
                    pThis->m_termHistoryIndex = (int)pThis->m_termHistory.size();
                    pThis->replaceCurrentInput("");
                }
            }
            return 0;
        }
    }

    if (pfnOld) {
        return CallWindowProcA(pfnOld, hWnd, msg, wParam, lParam);
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void UIEngine::setCommandHandler(CommandHandler handler) {
    m_commandHandler = handler;
}

void UIEngine::printTerminalText(const std::string& text) {
    if (!m_hTerminalEdit || !IsWindow(m_hTerminalEdit)) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wlen <= 0) return;
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wbuf.data(), wlen);

    int len = GetWindowTextLengthW(m_hTerminalEdit);
    SendMessageW(m_hTerminalEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(m_hTerminalEdit, EM_REPLACESEL, FALSE, (LPARAM)wbuf.data());
    SendMessageW(m_hTerminalEdit, EM_SCROLLCARET, 0, 0);
}

void UIEngine::clearTerminal() {
    if (m_hTerminalEdit && IsWindow(m_hTerminalEdit)) {
        SetWindowTextA(m_hTerminalEdit, "");
        printPrompt();
    }
}

void UIEngine::printPrompt() {
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
        std::string prompt = "SpecTer [" + std::string(cwd) + "]> ";
        printTerminalText(prompt);
    } else {
        printTerminalText("SpecTer [.]> ");
    }
}

void UIEngine::replaceCurrentInput(const std::string& input) {
    if (!m_hTerminalEdit || !IsWindow(m_hTerminalEdit)) return;

    int textLen = GetWindowTextLengthA(m_hTerminalEdit);
    std::vector<char> buf(textLen + 1, 0);
    GetWindowTextA(m_hTerminalEdit, buf.data(), textLen + 1);

    std::string fullText(buf.data());
    size_t lastPrompt = fullText.rfind("SpecTer [");
    if (lastPrompt != std::string::npos) {
        size_t arrowPos = fullText.find("]> ", lastPrompt);
        if (arrowPos != std::string::npos) {
            size_t inputStart = arrowPos + 3;
            SendMessageA(m_hTerminalEdit, EM_SETSEL, (WPARAM)inputStart, (LPARAM)textLen);
            SendMessageA(m_hTerminalEdit, EM_REPLACESEL, FALSE, (LPARAM)input.c_str());
        }
    }
}

} // namespace SpecTer
