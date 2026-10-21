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

// ── Ultra-Modern Renk Paleti (Cyberpunk Slate & Neon Telemetry) ──
namespace Theme {
    const COLORREF BG_MAIN         = RGB(11, 15, 23);     // Derin modern uzay siyahı / antrasit
    const COLORREF CARD_BG         = RGB(18, 25, 38);     // Modern kart panel arkaplanı
    const COLORREF CARD_BORDER     = RGB(36, 49, 72);     // İnce kart çerçevesi
    const COLORREF HEADER_BG       = RGB(14, 20, 30);     // Üst başlık barı
    const COLORREF TEXT_PRIMARY    = RGB(242, 246, 255);  // Parlak beyaz metin
    const COLORREF TEXT_MUTED      = RGB(130, 148, 172);  // Muted açıklama metni
    const COLORREF ACCENT_CYAN     = RGB(0, 229, 255);    // CPU / Neon Camgöbeği
    const COLORREF ACCENT_GREEN    = RGB(0, 230, 118);    // RAM / Neon Yeşil
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
            SetTextColor(hdcStatic, RGB(0, 230, 118)); // Cyberpunk Neon Green Terminal Text
            SetBkColor(hdcStatic, RGB(10, 14, 20));    // Dark Terminal Background
            if (!pThis->m_hTermBrush) {
                pThis->m_hTermBrush = CreateSolidBrush(RGB(10, 14, 20));
            }
            return (LRESULT)pThis->m_hTermBrush;
        }
        // Statik metin ve trackbar zeminlerini kart rengine uydur (gri kutu olmasın)
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
// Win32 Kontrollerinin Kurulumu (Modern Düzen)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::setupControls(HWND hWnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hWnd, GWLP_HINSTANCE);

    // 0. Sol Panel Gömülü Terminal Kontrolü (x: 18, y: 80, w: 668, h: 660)
    m_hTerminalEdit = CreateWindowExA(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        18, 80, 668, 660,
        hWnd, (HMENU)100, hInst, NULL
    );

    // Text limitini maksimum yap (~2GB)
    SendMessageA(m_hTerminalEdit, EM_LIMITTEXT, 0, 0);

    // Sabit genişlikli Monospace Font (Consolas)
    m_hTermFont = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (m_hTermFont) {
        SendMessageA(m_hTerminalEdit, WM_SETFONT, (WPARAM)m_hTermFont, TRUE);
    }

    // Subclass Terminal Edit Control
    SetWindowLongPtrA(m_hTerminalEdit, GWLP_USERDATA, (LONG_PTR)this);
    m_pfnOldEditProc = (WNDPROC)SetWindowLongPtrA(m_hTerminalEdit, GWLP_WNDPROC, (LONG_PTR)TerminalEditProc);

    HFONT hFontLabel = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    // 1. RAM Slider Etiketi
    HWND hLblRam = CreateWindowA("STATIC", "RAM Sınırı (MB):",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        725, 510, 130, 20,
        hWnd, NULL, hInst, NULL);
    SendMessage(hLblRam, WM_SETFONT, (WPARAM)hFontLabel, TRUE);

    m_hLblRamVal = CreateWindowA("STATIC", "256 MB",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        940, 510, 95, 20,
        hWnd, NULL, hInst, NULL);
    SendMessage(m_hLblRamVal, WM_SETFONT, (WPARAM)hFontLabel, TRUE);

    // RAM Trackbar (64 MB - 2048 MB)
    m_hTrackRam = CreateWindowA(TRACKBAR_CLASSA, "RAM Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        725, 532, 310, 26,
        hWnd, (HMENU)IDC_TRACK_RAM, hInst, NULL);
    SendMessage(m_hTrackRam, TBM_SETRANGE, TRUE, MAKELPARAM(64, 2048));
    SendMessage(m_hTrackRam, TBM_SETPOS, TRUE, 256);

    // 2. CPU Slider Etiketi
    HWND hLblCpu = CreateWindowA("STATIC", "CPU Kotası (%):",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        725, 568, 130, 20,
        hWnd, NULL, hInst, NULL);
    SendMessage(hLblCpu, WM_SETFONT, (WPARAM)hFontLabel, TRUE);

    m_hLblCpuVal = CreateWindowA("STATIC", "%50",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        940, 568, 95, 20,
        hWnd, NULL, hInst, NULL);
    SendMessage(m_hLblCpuVal, WM_SETFONT, (WPARAM)hFontLabel, TRUE);

    // CPU Trackbar (10% - 100%)
    m_hTrackCpu = CreateWindowA(TRACKBAR_CLASSA, "CPU Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        725, 590, 310, 26,
        hWnd, (HMENU)IDC_TRACK_CPU, hInst, NULL);
    SendMessage(m_hTrackCpu, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
    SendMessage(m_hTrackCpu, TBM_SETPOS, TRUE, 50);

    // 3. Sandbox Toggle Butonu (Özel Cyberpunk Çizim: BS_OWNERDRAW)
    m_hBtnToggleSandbox = CreateWindowA("BUTTON", "SANDBOX: KAPALI",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        725, 638, 150, 42,
        hWnd, (HMENU)IDC_BTN_TOGGLE_SB, hInst, NULL);

    // 4. Uygula (Apply) Butonu (Özel Cyberpunk Çizim: BS_OWNERDRAW)
    m_hBtnApply = CreateWindowA("BUTTON", "LİMİTLERİ UYGULA",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        885, 638, 150, 42,
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
        if (m_sandboxCallback) {
            m_sandboxCallback(m_state.sandboxConfig, m_state.sandboxEnabled);
        }
        if (m_hBtnToggleSandbox && IsWindow(m_hBtnToggleSandbox)) {
            InvalidateRect(m_hBtnToggleSandbox, NULL, TRUE);
        }
        if (m_hWnd && IsWindow(m_hWnd)) {
            InvalidateRect(m_hWnd, NULL, FALSE);
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
        if (m_hBtnApply && IsWindow(m_hBtnApply)) {
            InvalidateRect(m_hBtnApply, NULL, TRUE);
        }
        if (m_hWnd && IsWindow(m_hWnd)) {
            InvalidateRect(m_hWnd, NULL, FALSE);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Modern Owner-Draw Buton Çizimi (Cyberpunk Glowing Pill Buttons)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::onDrawItem(LPDRAWITEMSTRUCT pDIS) {
    if (!pDIS) return;
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    bool isPressed = (pDIS->itemState & ODS_SELECTED) != 0;

    SetBkMode(hdc, TRANSPARENT);
    HFONT hBtnFont = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hBtnFont);

    if (pDIS->CtlID == IDC_BTN_TOGGLE_SB) {
        bool sbEnabled = false;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            sbEnabled = m_state.sandboxEnabled;
        }

        COLORREF bgCol = sbEnabled 
            ? (isPressed ? RGB(10, 60, 32) : RGB(16, 75, 42)) 
            : (isPressed ? RGB(45, 20, 24) : RGB(36, 22, 28));
        COLORREF borderCol = sbEnabled ? Theme::ACCENT_GREEN : RGB(220, 60, 60);

        HBRUSH hBrush = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 2, borderCol);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        std::string text = sbEnabled ? "🛡️ SANDBOX: AKTİF" : "⚪ SANDBOX: KAPALI";
        DrawTextUtf8(hdc, text, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, sbEnabled ? Theme::ACCENT_GREEN : RGB(245, 120, 120));
    }
    else if (pDIS->CtlID == IDC_BTN_APPLY) {
        COLORREF bgCol = isPressed ? RGB(10, 48, 78) : RGB(15, 62, 102);
        COLORREF borderCol = Theme::ACCENT_CYAN;

        HBRUSH hBrush = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 2, borderCol);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        DrawTextUtf8(hdc, "⚡ LİMİTLERİ UYGULA", &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, RGB(255, 255, 255));
    }

    SelectObject(hdc, hOldFont);
    DeleteObject(hBtnFont);
}

// ─────────────────────────────────────────────────────────────────────────────
// Double-Buffered GDI Çizimi (Modern Rounded Cards & Sleek Telemetry)
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

    HFONT hFontTitle = CreateFontA(21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontSub = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontBold = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontGaugeVal = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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

    // Yeşil Durum LED'i
    HBRUSH hDotBrush = CreateSolidBrush(Theme::ACCENT_GREEN);
    HBRUSH hOldDot = (HBRUSH)SelectObject(hdc, hDotBrush);
    Ellipse(hdc, 16, 16, 26, 26);
    SelectObject(hdc, hOldDot);
    DeleteObject(hDotBrush);

    SelectObject(hdc, hFontTitle);
    TextOutUtf8(hdc, 34, 8, "SPECTER", Theme::ACCENT_CYAN);

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, 130, 13, ":: Kernel Observability & Sandboxed CLI/GUI Terminal", Theme::TEXT_MUTED);

    // Aktif Süreç Rozeti (Ortada)
    std::string procText = "⚡ Süreç: " + (stateCopy.processName.empty() ? "Beklemede..." : stateCopy.processName);
    if (stateCopy.activePID > 0) {
        procText += " [PID: " + std::to_string(stateCopy.activePID) + "]";
    }
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, 650, 11, procText, Theme::TEXT_PRIMARY);

    // Sağ Üst Sandbox Durum Rozeti (Yuvarlatılmış Pill)
    RECT rBadge = { width - 210, 6, width - 15, 34 };
    HBRUSH hBadgeBrush = CreateSolidBrush(stateCopy.sandboxEnabled ? RGB(12, 60, 32) : RGB(48, 22, 26));
    HPEN hBadgePen = CreatePen(PS_SOLID, 1, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : Theme::ACCENT_RED);
    SelectObject(hdc, hBadgeBrush);
    SelectObject(hdc, hBadgePen);
    RoundRect(hdc, rBadge.left, rBadge.top, rBadge.right, rBadge.bottom, 10, 10);
    DeleteObject(hBadgeBrush);
    DeleteObject(hBadgePen);

    std::string badgeStr = stateCopy.sandboxEnabled ? "🛡️ SANDBOX: AKTİF" : "⚪ SANDBOX: KAPALI";
    DrawTextUtf8(hdc, badgeStr, &rBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : Theme::ACCENT_RED);

    // ─────────────────────────────────────────────────────────────────────────
    // SOL TERMİNAL BÖLGESİ ÇERÇEVESİ (x: 12, y: 48, w: 680, h: 708)
    // ─────────────────────────────────────────────────────────────────────────
    RECT rLeftFrame = { 12, 48, 692, 756 };
    HBRUSH hLeftFrameBrush = CreateSolidBrush(RGB(10, 14, 20));
    HPEN hLeftPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    SelectObject(hdc, hLeftFrameBrush);
    SelectObject(hdc, hLeftPen);
    RoundRect(hdc, rLeftFrame.left, rLeftFrame.top, rLeftFrame.right, rLeftFrame.bottom, 14, 14);
    DeleteObject(hLeftFrameBrush);
    DeleteObject(hLeftPen);

    // Terminal Başlık Çubuğu
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, 24, 56, "📟 GÖMÜLÜ TERMİNAL (ShellCore REPL v1.0)", Theme::ACCENT_GREEN);

    HPEN hTermSep = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    SelectObject(hdc, hTermSep);
    MoveToEx(hdc, 20, 76, NULL);
    LineTo(hdc, 684, 76);
    DeleteObject(hTermSep);

    // ─────────────────────────────────────────────────────────────────────────
    // SAĞ PANEL YUVARLATILMIŞ KART ÇİZİM YARDIMCISI
    // ─────────────────────────────────────────────────────────────────────────
    auto drawCard = [&](int x, int y, int w, int h, const std::string& title) {
        HPEN hCardPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
        HBRUSH hCardBrush = CreateSolidBrush(Theme::CARD_BG);
        HPEN hOldP = (HPEN)SelectObject(hdc, hCardPen);
        HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hCardBrush);

        RoundRect(hdc, x, y, x + w, y + h, 14, 14);

        SelectObject(hdc, hOldB);
        SelectObject(hdc, hOldP);
        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);

        SelectObject(hdc, hFontBold);
        TextOutUtf8(hdc, x + 16, y + 10, title, Theme::ACCENT_CYAN);

        HPEN hSepPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
        SelectObject(hdc, hSepPen);
        MoveToEx(hdc, x + 14, y + 34, NULL);
        LineTo(hdc, x + w - 14, y + 34);
        DeleteObject(hSepPen);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // KART 1: [RAM & CPU GAUGES] (x: 705, y: 48, w: 350, h: 206)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(705, 48, 350, 206, "1. [CPU & RAM GAUGES] Canlı Metrikler");

    double cpuVal = std::clamp(stateCopy.metrics.cpuUsagePercent, 0.0, 100.0);
    COLORREF cpuColor = (cpuVal > 75.0) ? Theme::ACCENT_RED : ((cpuVal > 40.0) ? Theme::ACCENT_AMBER : Theme::ACCENT_CYAN);

    int gaugeCenterX = 775;
    int gaugeCenterY = 138;
    int gaugeRadius = 45;

    // Arka plan dairesel iz
    HPEN hTrackPen = CreatePen(PS_SOLID, 9, RGB(28, 38, 56));
    HPEN hOldTrackPen = (HPEN)SelectObject(hdc, hTrackPen);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY + gaugeRadius);
    SelectObject(hdc, hOldTrackPen);
    DeleteObject(hTrackPen);

    // Aktif yay (Arc)
    double angleRad = (cpuVal / 100.0) * 2.0 * 3.14159265 - (3.14159265 / 2.0);
    int endX = gaugeCenterX + (int)(gaugeRadius * std::cos(angleRad));
    int endY = gaugeCenterY + (int)(gaugeRadius * std::sin(angleRad));

    HPEN hCpuPen = CreatePen(PS_SOLID, 9, cpuColor);
    HPEN hOldCpuPen = (HPEN)SelectObject(hdc, hCpuPen);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY - gaugeRadius,
             endX, endY);
    SelectObject(hdc, hOldCpuPen);
    DeleteObject(hCpuPen);

    SelectObject(hdc, hFontGaugeVal);
    std::ostringstream ssCpu;
    ssCpu << std::fixed << std::setprecision(1) << cpuVal << "%";
    RECT rCpuVal = { gaugeCenterX - 45, gaugeCenterY - 14, gaugeCenterX + 45, gaugeCenterY + 14 };
    DrawTextUtf8(hdc, ssCpu.str(), &rCpuVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE, Theme::TEXT_PRIMARY);

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, gaugeCenterX - 34, gaugeCenterY + 48, "CPU YÜKÜ", Theme::TEXT_MUTED);

    // RAM Barları
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, 840, 88, "Bellek (Working Set):", Theme::TEXT_PRIMARY);

    std::string ramStr = std::to_string(stateCopy.metrics.workingSetSizeMB) + " MB (Peak: " +
                         std::to_string(stateCopy.metrics.peakWorkingSetSizeMB) + " MB)";
    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, 840, 108, ramStr, Theme::ACCENT_GREEN);

    // Yuvarlatılmış RAM Barı Yuvası
    RECT rRamBarBg = { 840, 130, 1035, 146 };
    HBRUSH hBarBg = CreateSolidBrush(RGB(28, 38, 56));
    SelectObject(hdc, hBarBg);
    RoundRect(hdc, rRamBarBg.left, rRamBarBg.top, rRamBarBg.right, rRamBarBg.bottom, 6, 6);
    DeleteObject(hBarBg);

    size_t maxLimitMB = (stateCopy.sandboxConfig.maxMemoryMB > 0) ? stateCopy.sandboxConfig.maxMemoryMB : 512;
    double ramRatio = std::clamp((double)stateCopy.metrics.workingSetSizeMB / (double)maxLimitMB, 0.0, 1.0);
    int barFillWidth = (int)((1035 - 840) * ramRatio);
    if (barFillWidth > 4) {
        RECT rRamBarFill = { 840, 130, 840 + barFillWidth, 146 };
        HBRUSH hBarFill = CreateSolidBrush(Theme::ACCENT_GREEN);
        SelectObject(hdc, hBarFill);
        RoundRect(hdc, rRamBarFill.left, rRamBarFill.top, rRamBarFill.right, rRamBarFill.bottom, 6, 6);
        DeleteObject(hBarFill);
    }

    std::string ramLimitStr = "Sandbox Sınırı: " + std::to_string(maxLimitMB) + " MB";
    TextOutUtf8(hdc, 840, 154, ramLimitStr, Theme::TEXT_MUTED);

    SelectObject(hdc, hFontBold);
    std::string threadTxt = "🧵 " + std::to_string(stateCopy.metrics.threadCount) + " Thread";
    TextOutUtf8(hdc, 840, 182, threadTxt, Theme::TEXT_PRIMARY);

    std::string handleTxt = "📂 " + std::to_string(stateCopy.metrics.openHandleCount) + " Handle";
    TextOutUtf8(hdc, 940, 182, handleTxt, Theme::TEXT_PRIMARY);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 2: [HANDLE TREE VIEW] (x: 705, y: 262, w: 350, h: 202)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(705, 262, 350, 202, "2. [KERNEL HANDLE TREE] Win32 Nesneleri");

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, 720, 298, "▼ İşletim Sistemi Kernel Tanıtıcı Hiyerarşisi:", Theme::TEXT_MUTED);

    std::string s1 = "   ├── ⚡ [Process Handle] : 0x" + std::to_string(stateCopy.activePID) + " (PROCESS_ALL_ACCESS)";
    TextOutUtf8(hdc, 720, 323, s1, Theme::ACCENT_CYAN);

    std::string s2 = "   ├── 🧵 [Thread Handles] : " + std::to_string(stateCopy.metrics.threadCount) + " Aktif İş Parçacığı (Toolhelp32)";
    TextOutUtf8(hdc, 720, 348, s2, Theme::TEXT_PRIMARY);

    std::string s3 = "   ├── 🔗 [Anonymous Pipes] : STDOUT & STDERR (SpecTer)";
    TextOutUtf8(hdc, 720, 373, s3, Theme::ACCENT_AMBER);

    std::string s4 = "   └── 🛡️ [Job Object] : " + std::string(stateCopy.sandboxEnabled ? "Bağlı (Kısıtlamalar Etkin)" : "Yok (Serbest Yürütme)");
    TextOutUtf8(hdc, 720, 398, s4, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : Theme::TEXT_MUTED);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 3: [SANDBOX PANEL] (x: 705, y: 472, w: 350, h: 284)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(705, 472, 350, 284, "3. [SANDBOX PANEL] Süreç Kısıtlama");

    // ─────────────────────────────────────────────────────────────────────────
    // KART 4: [REAL-TIME GRAPH] (x: 1065, y: 48, w: 400, h: 708)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(1065, 48, 400, 708, "4. [TIME GRAPH] 60sn Canlı Telemetri");

    int gx = 1105;
    int gy = 95;
    int gw = 345;
    int gh = 560;

    // Yuvarlatılmış Grafik Arkaplanı
    RECT rGraph = { gx, gy, gx + gw, gy + gh };
    HBRUSH hGraphBg = CreateSolidBrush(RGB(14, 20, 30));
    HPEN hGraphBorder = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    SelectObject(hdc, hGraphBg);
    SelectObject(hdc, hGraphBorder);
    RoundRect(hdc, rGraph.left, rGraph.top, rGraph.right, rGraph.bottom, 10, 10);
    DeleteObject(hGraphBg);
    DeleteObject(hGraphBorder);

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
        TextOutUtf8(hdc, xLine - 10, gy + gh + 6, tStr, Theme::TEXT_MUTED);
    }
    SelectObject(hdc, hOldVertPen);
    DeleteObject(hVertGridPen);

    if (historyCopy.size() >= 2) {
        // CPU Eğrisi (Kalın Neon Camgöbeği)
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

        // RAM Eğrisi (Kalın Neon Yeşil)
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

    // Lejant Göstergeleri (Pill Legend)
    int legY = gy + gh + 22;
    HPEN hCpuLeg = CreatePen(PS_SOLID, 3, Theme::ACCENT_CYAN);
    HPEN hOldCpuLeg = (HPEN)SelectObject(hdc, hCpuLeg);
    MoveToEx(hdc, gx + 15, legY + 8, NULL);
    LineTo(hdc, gx + 40, legY + 8);
    SelectObject(hdc, hOldCpuLeg);
    DeleteObject(hCpuLeg);
    TextOutUtf8(hdc, gx + 48, legY, "CPU (%)", Theme::TEXT_PRIMARY);

    HPEN hRamLeg = CreatePen(PS_SOLID, 3, Theme::ACCENT_GREEN);
    HPEN hOldRamLeg = (HPEN)SelectObject(hdc, hRamLeg);
    MoveToEx(hdc, gx + 180, legY + 8, NULL);
    LineTo(hdc, gx + 205, legY + 8);
    SelectObject(hdc, hOldRamLeg);
    DeleteObject(hRamLeg);
    TextOutUtf8(hdc, gx + 213, legY, "RAM (MB)", Theme::TEXT_PRIMARY);

    DeleteObject(hFontTitle);
    DeleteObject(hFontSub);
    DeleteObject(hFontBold);
    DeleteObject(hFontGaugeVal);

    BitBlt(hdcWindow, 0, 0, width, height, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdc);

    EndPaint(hWnd, &ps);
}

LRESULT CALLBACK UIEngine::TerminalEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UIEngine* pThis = (UIEngine*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
    WNDPROC pfnOld = pThis ? pThis->m_pfnOldEditProc : nullptr;

    if (msg == WM_KEYDOWN) {
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
