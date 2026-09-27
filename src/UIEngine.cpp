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
#define IDC_BTN_APPLY          1004
#define ID_TIMER_REFRESH       2001

namespace SpecTer {

// ── Renk Paleti (Sleek Dark Theme / Cyberpunk Glow) ──
namespace Theme {
    const COLORREF BG_MAIN         = RGB(15, 20, 28);     // Derin lacivert/antrasit
    const COLORREF CARD_BG         = RGB(24, 32, 47);     // Kart panel arkaplanı
    const COLORREF CARD_BORDER     = RGB(38, 50, 72);     // Kart çerçevesi
    const COLORREF HEADER_BG       = RGB(19, 26, 38);     // Üst bar
    const COLORREF TEXT_PRIMARY    = RGB(240, 245, 255);  // Parlak beyaz metin
    const COLORREF TEXT_MUTED      = RGB(130, 148, 172);  // Soluk açıklama metni
    const COLORREF ACCENT_CYAN     = RGB(0, 229, 255);    // CPU / Neon Camgöbeği
    const COLORREF ACCENT_GREEN    = RGB(0, 230, 118);    // RAM / Neon Yeşil
    const COLORREF ACCENT_AMBER    = RGB(255, 171, 0);    // Uyarı / Turuncu
    const COLORREF ACCENT_RED      = RGB(255, 82, 82);     // Kritik / Neon Kırmızı
    const COLORREF GRID_COLOR      = RGB(32, 42, 60);     // Grafik ızgara çizgisi
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

UIEngine::UIEngine()
    : m_hWnd(NULL)
    , m_running(false)
    , m_windowReady(false)
    , m_sandboxCallback(nullptr)
    , m_hTrackRam(NULL)
    , m_hTrackCpu(NULL)
    , m_hBtnToggleSandbox(NULL)
    , m_hBtnApply(NULL)
    , m_hLblRamVal(NULL)
    , m_hLblCpuVal(NULL)
    , m_hStaticBrush(CreateSolidBrush(Theme::CARD_BG))
    , m_hActiveProcess(NULL)
{
    // Varsayılan durum
    m_state.processName = "SpecTer Beklemede...";
    m_state.sandboxConfig.maxMemoryMB = 256;
    m_state.sandboxConfig.maxCpuPercent = 50;
    m_state.sandboxEnabled = false;

    // Geçmişi sıfırla doldur
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
}

bool UIEngine::initDashboardWindow(HINSTANCE hInstance) {
    if (m_running.load()) {
        return true;
    }

    m_running.store(true);
    m_windowReady.store(false);

    // GUI thread'ini başlat
    m_guiThread = std::thread(&UIEngine::guiThreadFunc, this, hInstance);

    // Pencerenin oluşturulmasını bekle (en fazla 3 saniye)
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

// ─────────────────────────────────────────────────────────────────────────────
// Arka Plan GUI Thread İşlevi (Tek Birleşik Pencere - Single Split Window)
// ─────────────────────────────────────────────────────────────────────────────
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

    // Tek Birleşik Pencere Boyutu: 1480 x 770 (Sol Terminal + Sağ Canlı Dashboard)
    const int winWidth = 1480;
    const int winHeight = 770;

    int posX = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;
    if (posX < 0) posX = 10;
    if (posY < 0) posY = 10;

    HWND hWnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        "SpecTer v1.0 | Observability & Sandboxed CLI/GUI Terminal",
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

    // --- Konsol Penceresini Sol Panele (x: 15, y: 50, w: 680, h: 695) Gömme ---
    HWND hConsoleWnd = GetConsoleWindow();
    if (hConsoleWnd != NULL && IsWindow(hConsoleWnd)) {
        LONG style = GetWindowLongA(hConsoleWnd, GWL_STYLE);
        style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        style |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
        SetWindowLongA(hConsoleWnd, GWL_STYLE, style);
        SetParent(hConsoleWnd, hWnd);
        SetWindowPos(hConsoleWnd, NULL, 15, 50, 680, 695, SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    setupControls(hWnd);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // Canlı profilleme & grafik yenileme için Win32 Timer (150ms)
    SetTimer(hWnd, ID_TIMER_REFRESH, 150, NULL);

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
            // Aktif çalışan süreç varsa canlı profilleme yap ve grafiği ilerlet
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

    case WM_HSCROLL:
        if (pThis) {
            pThis->handleHScroll(hWnd, wParam, lParam);
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
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
// Win32 Kontrollerinin Kurulumu (Sağ Panel Kart 3: x: 705, y: 475)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::setupControls(HWND hWnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hWnd, GWLP_HINSTANCE);

    // 1. RAM Slider Etiketi
    CreateWindowA("STATIC", "RAM Limiti (MB):",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        720, 510, 120, 20,
        hWnd, NULL, hInst, NULL);

    m_hLblRamVal = CreateWindowA("STATIC", "256 MB",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        845, 510, 60, 20,
        hWnd, NULL, hInst, NULL);

    // RAM Trackbar (64 MB - 2048 MB)
    m_hTrackRam = CreateWindowA(TRACKBAR_CLASSA, "RAM Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        720, 530, 185, 25,
        hWnd, (HMENU)IDC_TRACK_RAM, hInst, NULL);
    SendMessage(m_hTrackRam, TBM_SETRANGE, TRUE, MAKELPARAM(64, 2048));
    SendMessage(m_hTrackRam, TBM_SETPOS, TRUE, 256);

    // 2. CPU Slider Etiketi
    CreateWindowA("STATIC", "CPU Limiti (%):",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        720, 565, 110, 20,
        hWnd, NULL, hInst, NULL);

    m_hLblCpuVal = CreateWindowA("STATIC", "%50",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        855, 565, 50, 20,
        hWnd, NULL, hInst, NULL);

    // CPU Trackbar (10% - 100%)
    m_hTrackCpu = CreateWindowA(TRACKBAR_CLASSA, "CPU Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        720, 585, 185, 25,
        hWnd, (HMENU)IDC_TRACK_CPU, hInst, NULL);
    SendMessage(m_hTrackCpu, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
    SendMessage(m_hTrackCpu, TBM_SETPOS, TRUE, 50);

    // 3. Sandbox Toggle Butonu
    m_hBtnToggleSandbox = CreateWindowA("BUTTON", "SANDBOX: KAPALI",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        720, 630, 150, 35,
        hWnd, (HMENU)IDC_BTN_TOGGLE_SB, hInst, NULL);

    // 4. Uygula (Apply) Butonu
    m_hBtnApply = CreateWindowA("BUTTON", "LİMİTLERİ UYGULA",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        880, 630, 150, 35,
        hWnd, (HMENU)IDC_BTN_APPLY, hInst, NULL);
}

void UIEngine::handleHScroll(HWND /*hWnd*/, WPARAM /*wParam*/, LPARAM lParam) {
    if ((HWND)lParam == m_hTrackRam) {
        int pos = (int)SendMessage(m_hTrackRam, TBM_GETPOS, 0, 0);
        std::string txt = std::to_string(pos) + " MB";
        SetWindowTextA(m_hLblRamVal, txt.c_str());
    } else if ((HWND)lParam == m_hTrackCpu) {
        int pos = (int)SendMessage(m_hTrackCpu, TBM_GETPOS, 0, 0);
        std::string txt = "%" + std::to_string(pos);
        SetWindowTextA(m_hLblCpuVal, txt.c_str());
    }
}

void UIEngine::handleCommand(HWND /*hWnd*/, WPARAM wParam, LPARAM /*lParam*/) {
    int id = LOWORD(wParam);

    if (id == IDC_BTN_TOGGLE_SB) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_state.sandboxEnabled = !m_state.sandboxEnabled;
        if (m_state.sandboxEnabled) {
            SetWindowTextA(m_hBtnToggleSandbox, "SANDBOX: AKTİF [ON]");
        } else {
            SetWindowTextA(m_hBtnToggleSandbox, "SANDBOX: KAPALI [OFF]");
        }
        if (m_sandboxCallback) {
            m_sandboxCallback(m_state.sandboxConfig, m_state.sandboxEnabled);
        }
    } else if (id == IDC_BTN_APPLY) {
        int ramVal = (int)SendMessage(m_hTrackRam, TBM_GETPOS, 0, 0);
        int cpuVal = (int)SendMessage(m_hTrackCpu, TBM_GETPOS, 0, 0);

        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_state.sandboxConfig.maxMemoryMB = static_cast<size_t>(ramVal);
        m_state.sandboxConfig.maxCpuPercent = static_cast<DWORD>(cpuVal);

        if (m_sandboxCallback) {
            m_sandboxCallback(m_state.sandboxConfig, m_state.sandboxEnabled);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Double-Buffered GDI Çizimi (Tek Pencere Split Layout)
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

    HFONT hFontTitle = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontSub = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontBold = CreateFontA(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    SetBkMode(hdc, TRANSPARENT);

    // ─────────────────────────────────────────────────────────────────────────
    // 1. ÜST BAŞLIK BARI (Header Bar, y: 0..40)
    // ─────────────────────────────────────────────────────────────────────────
    RECT rHeader = { 0, 0, width, 40 };
    HBRUSH hHeaderBrush = CreateSolidBrush(Theme::HEADER_BG);
    FillRect(hdc, &rHeader, hHeaderBrush);
    DeleteObject(hHeaderBrush);

    HPEN hBorderPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
    MoveToEx(hdc, 0, 40, NULL);
    LineTo(hdc, width, 40);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBorderPen);

    SelectObject(hdc, hFontTitle);
    TextOutUtf8(hdc, 15, 8, "SPECTER", Theme::ACCENT_CYAN);

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, 110, 12, ":: Kernel Observability & Sandboxed CLI/GUI Terminal", Theme::TEXT_MUTED);

    std::string procText = "Aktif Süreç: " + (stateCopy.processName.empty() ? "Yok (Shell Beklemede)" : stateCopy.processName);
    if (stateCopy.activePID > 0) {
        procText += " [PID: " + std::to_string(stateCopy.activePID) + "]";
    }
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, 650, 10, procText, Theme::TEXT_PRIMARY);

    RECT rBadge = { width - 210, 6, width - 15, 34 };
    HBRUSH hBadgeBrush = CreateSolidBrush(stateCopy.sandboxEnabled ? RGB(10, 60, 30) : RGB(50, 30, 30));
    FillRect(hdc, &rBadge, hBadgeBrush);
    DeleteObject(hBadgeBrush);
    FrameRect(hdc, &rBadge, (HBRUSH)GetStockObject(WHITE_BRUSH));

    std::string badgeStr = stateCopy.sandboxEnabled ? "SANDBOX: AKTİF" : "SANDBOX: DEVRE DIŞI";
    DrawTextUtf8(hdc, badgeStr, &rBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : Theme::ACCENT_RED);

    // Sol Terminal Bölgesi Çerçevesi (x: 10, y: 45, w: 685, h: 710)
    RECT rLeftFrame = { 10, 45, 695, 755 };
    HBRUSH hLeftFrameBrush = CreateSolidBrush(RGB(10, 14, 20));
    FillRect(hdc, &rLeftFrame, hLeftFrameBrush);
    DeleteObject(hLeftFrameBrush);
    FrameRect(hdc, &rLeftFrame, (HBRUSH)GetStockObject(GRAY_BRUSH));

    // ─────────────────────────────────────────────────────────────────────────
    // SAĞ PANEL KART YARDIMCISI
    // ─────────────────────────────────────────────────────────────────────────
    auto drawCard = [&](int x, int y, int w, int h, const std::string& title) {
        RECT rc = { x, y, x + w, y + h };
        HBRUSH hCardBrush = CreateSolidBrush(Theme::CARD_BG);
        FillRect(hdc, &rc, hCardBrush);
        DeleteObject(hCardBrush);

        HPEN hCardPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
        HPEN hOldCardPen = (HPEN)SelectObject(hdc, hCardPen);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, x, y, x + w, y + h);
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldCardPen);
        DeleteObject(hCardPen);

        SelectObject(hdc, hFontBold);
        TextOutUtf8(hdc, x + 12, y + 8, title, Theme::ACCENT_CYAN);

        HPEN hSepPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
        HPEN hOldSepPen = (HPEN)SelectObject(hdc, hSepPen);
        MoveToEx(hdc, x + 12, y + 30, NULL);
        LineTo(hdc, x + w - 12, y + 30);
        SelectObject(hdc, hOldSepPen);
        DeleteObject(hSepPen);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // KART 1: [RAM & CPU GAUGES] (x: 705, y: 45, w: 350, h: 210)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(705, 45, 350, 210, "1. [RAM & CPU GAUGES] Canlı Metrikler");

    double cpuVal = std::clamp(stateCopy.metrics.cpuUsagePercent, 0.0, 100.0);
    COLORREF cpuColor = (cpuVal > 75.0) ? Theme::ACCENT_RED : ((cpuVal > 40.0) ? Theme::ACCENT_AMBER : Theme::ACCENT_CYAN);

    int gaugeCenterX = 770;
    int gaugeCenterY = 135;
    int gaugeRadius = 45;

    HPEN hTrackPen = CreatePen(PS_SOLID, 8, RGB(35, 48, 70));
    HPEN hOldTrackPen = (HPEN)SelectObject(hdc, hTrackPen);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY + gaugeRadius);
    SelectObject(hdc, hOldTrackPen);
    DeleteObject(hTrackPen);

    double angleRad = (cpuVal / 100.0) * 2.0 * 3.14159265 - (3.14159265 / 2.0);
    int endX = gaugeCenterX + (int)(gaugeRadius * std::cos(angleRad));
    int endY = gaugeCenterY + (int)(gaugeRadius * std::sin(angleRad));

    HPEN hCpuPen = CreatePen(PS_SOLID, 8, cpuColor);
    HPEN hOldCpuPen = (HPEN)SelectObject(hdc, hCpuPen);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY - gaugeRadius,
             endX, endY);
    SelectObject(hdc, hOldCpuPen);
    DeleteObject(hCpuPen);

    SelectObject(hdc, hFontBold);
    std::ostringstream ssCpu;
    ssCpu << std::fixed << std::setprecision(1) << cpuVal << "%";
    RECT rCpuVal = { gaugeCenterX - 40, gaugeCenterY - 10, gaugeCenterX + 40, gaugeCenterY + 10 };
    DrawTextUtf8(hdc, ssCpu.str(), &rCpuVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE, Theme::TEXT_PRIMARY);

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, gaugeCenterX - 35, gaugeCenterY + 50, "CPU KULLANIMI", Theme::TEXT_MUTED);

    // RAM Barları
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, 840, 90, "Bellek (Working Set):", Theme::TEXT_PRIMARY);

    std::string ramStr = std::to_string(stateCopy.metrics.workingSetSizeMB) + " MB (Peak: " +
                         std::to_string(stateCopy.metrics.peakWorkingSetSizeMB) + " MB)";
    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, 840, 110, ramStr, Theme::ACCENT_GREEN);

    RECT rRamBarBg = { 840, 132, 1040, 147 };
    HBRUSH hBarBg = CreateSolidBrush(RGB(35, 48, 70));
    FillRect(hdc, &rRamBarBg, hBarBg);
    DeleteObject(hBarBg);

    size_t maxLimitMB = (stateCopy.sandboxConfig.maxMemoryMB > 0) ? stateCopy.sandboxConfig.maxMemoryMB : 512;
    double ramRatio = std::clamp((double)stateCopy.metrics.workingSetSizeMB / (double)maxLimitMB, 0.0, 1.0);
    int barFillWidth = (int)((1040 - 840) * ramRatio);
    RECT rRamBarFill = { 840, 132, 840 + barFillWidth, 147 };
    HBRUSH hBarFill = CreateSolidBrush(Theme::ACCENT_GREEN);
    FillRect(hdc, &rRamBarFill, hBarFill);
    DeleteObject(hBarFill);

    std::string ramLimitStr = "Sandbox Limiti: " + std::to_string(maxLimitMB) + " MB";
    TextOutUtf8(hdc, 840, 155, ramLimitStr, Theme::TEXT_MUTED);

    SelectObject(hdc, hFontBold);
    std::string threadTxt = "🧵 Threads: " + std::to_string(stateCopy.metrics.threadCount);
    TextOutUtf8(hdc, 840, 185, threadTxt, Theme::TEXT_PRIMARY);

    std::string handleTxt = "📂 Handles: " + std::to_string(stateCopy.metrics.openHandleCount);
    TextOutUtf8(hdc, 940, 185, handleTxt, Theme::TEXT_PRIMARY);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 2: [HANDLE TREE VIEW] (x: 705, y: 260, w: 350, h: 205)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(705, 260, 350, 205, "2. [HANDLE TREE] Kernel Hiyerarşisi");

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, 715, 298, "▼ Süreç Kernel Nesneleri (Win32 Object Manager):", Theme::TEXT_MUTED);

    std::string s1 = "   ├── [Process Handle] : 0x" + std::to_string(stateCopy.activePID) + " (PROCESS_ALL_ACCESS)";
    TextOutUtf8(hdc, 715, 323, s1, Theme::TEXT_PRIMARY);

    std::string s2 = "   ├── [Thread Handles] : " + std::to_string(stateCopy.metrics.threadCount) + " Aktif İş Parçacığı (Toolhelp32)";
    TextOutUtf8(hdc, 715, 348, s2, Theme::TEXT_PRIMARY);

    std::string s3 = "   ├── [Pipes (STDOUT/STDERR)] : Anonymous Pipe (SpecTer)";
    TextOutUtf8(hdc, 715, 373, s3, Theme::TEXT_PRIMARY);

    std::string s4 = "   └── [Job Object] : " + std::string(stateCopy.sandboxEnabled ? "Bağlı (Kısıtlamalar Etkin)" : "Yok (Serbest Yürütme)");
    TextOutUtf8(hdc, 715, 398, s4, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : Theme::TEXT_MUTED);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 3: [SANDBOX PANEL] (x: 705, y: 475, w: 360, h: 280)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(705, 475, 360, 280, "3. [SANDBOX PANEL] Sınırlandırma");

    // ─────────────────────────────────────────────────────────────────────────
    // KART 4: [REAL-TIME GRAPH] (x: 1075, y: 45, w: 390, h: 710)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(1075, 45, 390, 710, "4. [REAL-TIME GRAPH] Canlı Zaman Serisi (Son 60sn)");

    int gx = 1115;
    int gy = 95;
    int gw = 330;
    int gh = 560;

    RECT rGraph = { gx, gy, gx + gw, gy + gh };
    HBRUSH hGraphBg = CreateSolidBrush(RGB(18, 24, 34));
    FillRect(hdc, &rGraph, hGraphBg);
    DeleteObject(hGraphBg);

    SelectObject(hdc, hFontSub);
    HPEN hGridPen = CreatePen(PS_DOT, 1, Theme::GRID_COLOR);
    HPEN hOldGridPen = (HPEN)SelectObject(hdc, hGridPen);

    for (int step = 0; step <= 4; ++step) {
        int yLine = gy + (gh * step) / 4;
        MoveToEx(hdc, gx, yLine, NULL);
        LineTo(hdc, gx + gw, yLine);

        int pct = 100 - step * 25;
        std::string lbl = std::to_string(pct) + "%";
        TextOutUtf8(hdc, gx - 32, yLine - 8, lbl, Theme::TEXT_MUTED);

        size_t ramStepVal = (maxLimitMB * (4 - step)) / 4;
        std::string ramStepStr = std::to_string(ramStepVal) + "M";
        TextOutUtf8(hdc, gx + gw + 4, yLine - 8, ramStepStr, Theme::ACCENT_GREEN);
    }
    SelectObject(hdc, hOldGridPen);
    DeleteObject(hGridPen);

    HPEN hVertGridPen = CreatePen(PS_DOT, 1, Theme::GRID_COLOR);
    HPEN hOldVertPen = (HPEN)SelectObject(hdc, hVertGridPen);
    for (int step = 0; step <= 4; ++step) {
        int xLine = gx + (gw * step) / 4;
        MoveToEx(hdc, xLine, gy, NULL);
        LineTo(hdc, xLine, gy + gh);

        int sec = -60 + step * 15;
        std::string tStr = (sec == 0) ? "0s" : (std::to_string(sec) + "s");
        TextOutUtf8(hdc, xLine - 10, gy + gh + 4, tStr, Theme::TEXT_MUTED);
    }
    SelectObject(hdc, hOldVertPen);
    DeleteObject(hVertGridPen);

    if (historyCopy.size() >= 2) {
        // CPU Eğrisi (Neon Camgöbeği)
        HPEN hCpuGraphPen = CreatePen(PS_SOLID, 2, Theme::ACCENT_CYAN);
        HPEN hOldCpuGraphPen = (HPEN)SelectObject(hdc, hCpuGraphPen);

        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double cVal = std::clamp(historyCopy[i].cpuUsagePercent, 0.0, 100.0);
            int ptX = gx + (int)((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = gy + gh - (int)((gh * cVal) / 100.0);

            if (i == 0) {
                MoveToEx(hdc, ptX, ptY, NULL);
            } else {
                LineTo(hdc, ptX, ptY);
            }
        }
        SelectObject(hdc, hOldCpuGraphPen);
        DeleteObject(hCpuGraphPen);

        // RAM Eğrisi (Neon Yeşil)
        HPEN hRamGraphPen = CreatePen(PS_SOLID, 2, Theme::ACCENT_GREEN);
        HPEN hOldRamGraphPen = (HPEN)SelectObject(hdc, hRamGraphPen);

        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double rVal = std::clamp((double)historyCopy[i].ramMB / (double)maxLimitMB, 0.0, 1.0);
            int ptX = gx + (int)((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = gy + gh - (int)(gh * rVal);

            if (i == 0) {
                MoveToEx(hdc, ptX, ptY, NULL);
            } else {
                LineTo(hdc, ptX, ptY);
            }
        }
        SelectObject(hdc, hOldRamGraphPen);
        DeleteObject(hRamGraphPen);
    }

    int legY = gy + gh + 22;
    HPEN hCpuLeg = CreatePen(PS_SOLID, 3, Theme::ACCENT_CYAN);
    HPEN hOldCpuLeg = (HPEN)SelectObject(hdc, hCpuLeg);
    MoveToEx(hdc, gx + 10, legY + 8, NULL);
    LineTo(hdc, gx + 35, legY + 8);
    SelectObject(hdc, hOldCpuLeg);
    DeleteObject(hCpuLeg);
    TextOutUtf8(hdc, gx + 42, legY, "CPU (%)", Theme::TEXT_PRIMARY);

    HPEN hRamLeg = CreatePen(PS_SOLID, 3, Theme::ACCENT_GREEN);
    HPEN hOldRamLeg = (HPEN)SelectObject(hdc, hRamLeg);
    MoveToEx(hdc, gx + 170, legY + 8, NULL);
    LineTo(hdc, gx + 195, legY + 8);
    SelectObject(hdc, hOldRamLeg);
    DeleteObject(hRamLeg);
    TextOutUtf8(hdc, gx + 202, legY, "RAM (MB)", Theme::TEXT_PRIMARY);

    DeleteObject(hFontTitle);
    DeleteObject(hFontSub);
    DeleteObject(hFontBold);

    BitBlt(hdcWindow, 0, 0, width, height, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdc);

    EndPaint(hWnd, &ps);
}

} // namespace SpecTer
