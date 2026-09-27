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
{
    // Varsayılan durum
    m_state.processName = "SpecTer Beklemede...";
    m_state.sandboxConfig.maxMemoryMB = 256;
    m_state.sandboxConfig.maxCpuPercent = 50;
    m_state.sandboxEnabled = false;

    // Geçmişi sıfırla doldur (grafik başlangıçta dolu görünsün)
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
        return true; // Zaten çalışıyor
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

void UIEngine::updateDashboard(const DashboardState& state) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_state = state;

        // Geçmiş verisine yeni örnek ekle
        MetricSample sample;
        sample.cpuUsagePercent = state.metrics.cpuUsagePercent;
        sample.ramMB = state.metrics.workingSetSizeMB;

        m_history.push_back(sample);
        while (m_history.size() > MAX_HISTORY_POINTS) {
            m_history.pop_front();
        }
    }

    // Pencereyi yeniden çizdir
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
// Arka Plan GUI Thread İşlevi
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::guiThreadFunc(HINSTANCE hInstance) {
    if (!hInstance) {
        hInstance = GetModuleHandle(NULL);
    }

    // Windows Common Controls başlat (Slider / Trackbar için)
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

    // Pencere Boyutu: 980 x 680 (Sabit & Şık)
    const int winWidth = 980;
    const int winHeight = 690;

    // Ekranın ortasında veya sağ üst köşesinde başlat
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;
    if (posX < 0) posX = 50;
    if (posY < 0) posY = 50;

    HWND hWnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        "SpecTer :: Canlı Sistem İzleme ve Sandbox Denetim Paneli",
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

    // Canlı yenileme için Win32 Timer (50ms = 20 FPS)
    SetTimer(hWnd, ID_TIMER_REFRESH, 50, NULL);

    m_windowReady.store(true);

    // Win32 Mesaj Döngüsü (Message Loop)
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
        if (wParam == ID_TIMER_REFRESH) {
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
// Win32 Kontrollerinin Kurulumu (Slider, Butonlar)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::setupControls(HWND hWnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hWnd, GWLP_HINSTANCE);

    // Sandbox Paneli Kartı İçinde Kontroller:
    // Konum: x: 30, y: 510, genişlik: 420, yükseklik: 140

    // 1. RAM Slider Etiketi
    CreateWindowA("STATIC", "RAM Limiti (MB):",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        35, 515, 120, 20,
        hWnd, NULL, hInst, NULL);

    m_hLblRamVal = CreateWindowA("STATIC", "256 MB",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        160, 515, 60, 20,
        hWnd, NULL, hInst, NULL);

    // RAM Trackbar (64 MB - 2048 MB)
    m_hTrackRam = CreateWindowA(TRACKBAR_CLASSA, "RAM Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        35, 535, 190, 25,
        hWnd, (HMENU)IDC_TRACK_RAM, hInst, NULL);
    SendMessage(m_hTrackRam, TBM_SETRANGE, TRUE, MAKELPARAM(64, 2048));
    SendMessage(m_hTrackRam, TBM_SETPOS, TRUE, 256);

    // 2. CPU Slider Etiketi
    CreateWindowA("STATIC", "CPU Limiti (%):",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        245, 515, 110, 20,
        hWnd, NULL, hInst, NULL);

    m_hLblCpuVal = CreateWindowA("STATIC", "%50",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        360, 515, 50, 20,
        hWnd, NULL, hInst, NULL);

    // CPU Trackbar (10% - 100%)
    m_hTrackCpu = CreateWindowA(TRACKBAR_CLASSA, "CPU Slider",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        245, 535, 175, 25,
        hWnd, (HMENU)IDC_TRACK_CPU, hInst, NULL);
    SendMessage(m_hTrackCpu, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
    SendMessage(m_hTrackCpu, TBM_SETPOS, TRUE, 50);

    // 3. Sandbox Toggle Butonu
    m_hBtnToggleSandbox = CreateWindowA("BUTTON", "SANDBOX: KAPALI",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        35, 575, 190, 32,
        hWnd, (HMENU)IDC_BTN_TOGGLE_SB, hInst, NULL);

    // 4. Uygula (Apply) Butonu
    m_hBtnApply = CreateWindowA("BUTTON", "LİMİTLERİ UYGULA",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        245, 575, 175, 32,
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
// Double-Buffered GDI Çizimi
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::onPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdcWindow = BeginPaint(hWnd, &ps);

    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int width = clientRect.right;
    int height = clientRect.bottom;

    // Double buffering için hafıza DC ve Bitmap'i oluştur
    HDC hdc = CreateCompatibleDC(hdcWindow);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcWindow, width, height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdc, hbmMem);

    // Arkaplanı temizle
    HBRUSH hBgBrush = CreateSolidBrush(Theme::BG_MAIN);
    FillRect(hdc, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    // Durumu kopyala (thread-safe)
    DashboardState stateCopy;
    std::vector<MetricSample> historyCopy;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        stateCopy = m_state;
        historyCopy.assign(m_history.begin(), m_history.end());
    }

    // Font tanımları
    HFONT hFontTitle = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontSub = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontBold = CreateFontA(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontLarge = CreateFontA(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    SetBkMode(hdc, TRANSPARENT);

    // ─────────────────────────────────────────────────────────────────────────
    // 1. ÜST BAŞLIK BARI (Header Bar, y: 0..70)
    // ─────────────────────────────────────────────────────────────────────────
    RECT rHeader = { 0, 0, width, 70 };
    HBRUSH hHeaderBrush = CreateSolidBrush(Theme::HEADER_BG);
    FillRect(hdc, &rHeader, hHeaderBrush);
    DeleteObject(hHeaderBrush);

    // Alt çizgi
    HPEN hBorderPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hBorderPen);
    MoveToEx(hdc, 0, 70, NULL);
    LineTo(hdc, width, 70);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBorderPen);

    // SpecTer Başlığı & Rozeti
    SelectObject(hdc, hFontTitle);
    SetTextColor(hdc, Theme::ACCENT_CYAN);
    TextOutA(hdc, 25, 12, "SPECTER", 7);

    SelectObject(hdc, hFontSub);
    SetTextColor(hdc, Theme::TEXT_MUTED);
    TextOutA(hdc, 125, 18, ":: Kernel Observability & Live Visualizer Shell", 47);

    // Aktif Süreç Bilgi Kartı (Header Sağ)
    std::string procText = "Aktif Süreç: " + (stateCopy.processName.empty() ? "Yok (Shell Beklemede)" : stateCopy.processName);
    if (stateCopy.activePID > 0) {
        procText += " [PID: " + std::to_string(stateCopy.activePID) + "]";
    }
    SetTextColor(hdc, Theme::TEXT_PRIMARY);
    SelectObject(hdc, hFontBold);
    TextOutA(hdc, 25, 42, procText.c_str(), (int)procText.length());

    // Sandbox Durum Rozeti (Header En Sağ)
    RECT rBadge = { width - 230, 18, width - 25, 52 };
    HBRUSH hBadgeBrush = CreateSolidBrush(stateCopy.sandboxEnabled ? RGB(10, 60, 30) : RGB(50, 30, 30));
    FillRect(hdc, &rBadge, hBadgeBrush);
    DeleteObject(hBadgeBrush);
    FrameRect(hdc, &rBadge, (HBRUSH)GetStockObject(WHITE_BRUSH));

    SetTextColor(hdc, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : Theme::ACCENT_RED);
    std::string badgeStr = stateCopy.sandboxEnabled ? "SANDBOX: AKTIF" : "SANDBOX: DEVRE DISI";
    DrawTextA(hdc, badgeStr.c_str(), -1, &rBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ─────────────────────────────────────────────────────────────────────────
    // KART YARDIMCISI LAMBDA (Yuvarlak / Çerçeveli Kart Paneli)
    // ─────────────────────────────────────────────────────────────────────────
    auto drawCard = [&](int x, int y, int w, int h, const char* title) {
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

        // Başlık
        SelectObject(hdc, hFontBold);
        SetTextColor(hdc, Theme::ACCENT_CYAN);
        TextOutA(hdc, x + 15, y + 12, title, (int)strlen(title));

        // Başlık altı ince çizgi
        HPEN hSepPen = CreatePen(PS_SOLID, 1, Theme::CARD_BORDER);
        HPEN hOldSepPen = (HPEN)SelectObject(hdc, hSepPen);
        MoveToEx(hdc, x + 15, y + 36, NULL);
        LineTo(hdc, x + w - 15, y + 36);
        SelectObject(hdc, hOldSepPen);
        DeleteObject(hSepPen);
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 2. KART 1: [RAM & CPU GAUGES] (x: 20, y: 85, w: 440, h: 220)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(20, 85, 440, 220, "1. [RAM & CPU GAUGES] Canlı Süreç Tüketimi");

    // CPU Radial / Bar Gauge (Sol Taraf)
    double cpuVal = std::clamp(stateCopy.metrics.cpuUsagePercent, 0.0, 100.0);
    COLORREF cpuColor = (cpuVal > 75.0) ? Theme::ACCENT_RED : ((cpuVal > 40.0) ? Theme::ACCENT_AMBER : Theme::ACCENT_CYAN);

    // CPU Dairesel Gösterge Çizimi (Radial Arc Simülasyonu)
    int gaugeCenterX = 100;
    int gaugeCenterY = 170;
    int gaugeRadius = 50;

    // Dış Halka Arkaplanı
    HPEN hTrackPen = CreatePen(PS_SOLID, 10, RGB(35, 48, 70));
    HPEN hOldTrackPen = (HPEN)SelectObject(hdc, hTrackPen);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY + gaugeRadius);
    SelectObject(hdc, hOldTrackPen);
    DeleteObject(hTrackPen);

    // Aktif Yay (CPU Derecesi)
    double angleRad = (cpuVal / 100.0) * 2.0 * 3.14159265 - (3.14159265 / 2.0);
    int endX = gaugeCenterX + (int)(gaugeRadius * std::cos(angleRad));
    int endY = gaugeCenterY + (int)(gaugeRadius * std::sin(angleRad));

    HPEN hCpuPen = CreatePen(PS_SOLID, 10, cpuColor);
    HPEN hOldCpuPen = (HPEN)SelectObject(hdc, hCpuPen);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX, gaugeCenterY - gaugeRadius,
             endX, endY);
    SelectObject(hdc, hOldCpuPen);
    DeleteObject(hCpuPen);

    // CPU Yüzde Metni (Daire İçi)
    SelectObject(hdc, hFontBold);
    SetTextColor(hdc, Theme::TEXT_PRIMARY);
    std::ostringstream ssCpu;
    ssCpu << std::fixed << std::setprecision(1) << cpuVal << "%";
    RECT rCpuVal = { gaugeCenterX - 45, gaugeCenterY - 12, gaugeCenterX + 45, gaugeCenterY + 12 };
    DrawTextA(hdc, ssCpu.str().c_str(), -1, &rCpuVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hFontSub);
    SetTextColor(hdc, Theme::TEXT_MUTED);
    TextOutA(hdc, gaugeCenterX - 30, gaugeCenterY + 58, "CPU KULLANIMI", 13);

    // RAM Bilgi Barları (Sağ Taraf: x: 190..440)
    SelectObject(hdc, hFontBold);
    SetTextColor(hdc, Theme::TEXT_PRIMARY);
    TextOutA(hdc, 200, 130, "Bellek (Working Set):", 21);

    std::string ramStr = std::to_string(stateCopy.metrics.workingSetSizeMB) + " MB (Peak: " +
                         std::to_string(stateCopy.metrics.peakWorkingSetSizeMB) + " MB)";
    SelectObject(hdc, hFontSub);
    SetTextColor(hdc, Theme::ACCENT_GREEN);
    TextOutA(hdc, 200, 150, ramStr.c_str(), (int)ramStr.length());

    // RAM Bar Çizimi
    RECT rRamBarBg = { 200, 172, 430, 187 };
    HBRUSH hBarBg = CreateSolidBrush(RGB(35, 48, 70));
    FillRect(hdc, &rRamBarBg, hBarBg);
    DeleteObject(hBarBg);

    size_t maxLimitMB = (stateCopy.sandboxConfig.maxMemoryMB > 0) ? stateCopy.sandboxConfig.maxMemoryMB : 512;
    double ramRatio = std::clamp((double)stateCopy.metrics.workingSetSizeMB / (double)maxLimitMB, 0.0, 1.0);
    int barFillWidth = (int)((430 - 200) * ramRatio);
    RECT rRamBarFill = { 200, 172, 200 + barFillWidth, 187 };
    HBRUSH hBarFill = CreateSolidBrush(Theme::ACCENT_GREEN);
    FillRect(hdc, &rRamBarFill, hBarFill);
    DeleteObject(hBarFill);

    // Sandbox Limit Çizgisi ve Etiketi
    std::string ramLimitStr = "Sandbox Limiti: " + std::to_string(maxLimitMB) + " MB";
    SetTextColor(hdc, Theme::TEXT_MUTED);
    TextOutA(hdc, 200, 195, ramLimitStr.c_str(), (int)ramLimitStr.length());

    // Thread & Handle Sayaçları Rozetleri (Alt Kısım)
    SelectObject(hdc, hFontBold);
    SetTextColor(hdc, Theme::TEXT_PRIMARY);
    std::string threadTxt = "🧵 Threads: " + std::to_string(stateCopy.metrics.threadCount);
    TextOutA(hdc, 200, 235, threadTxt.c_str(), (int)threadTxt.length());

    std::string handleTxt = "📂 Handles: " + std::to_string(stateCopy.metrics.openHandleCount);
    TextOutA(hdc, 320, 235, handleTxt.c_str(), (int)handleTxt.length());

    // ─────────────────────────────────────────────────────────────────────────
    // 3. KART 2: [HANDLE TREE VIEW] (x: 20, y: 315, w: 440, h: 160)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(20, 315, 440, 160, "2. [HANDLE TREE VIEW] Kernel Kaynak Hiyerarşisi");

    SelectObject(hdc, hFontSub);
    SetTextColor(hdc, Theme::TEXT_MUTED);
    TextOutA(hdc, 35, 355, "▼ Süreç Kernel Nesneleri (Win32 Object Manager):", 48);

    SetTextColor(hdc, Theme::TEXT_PRIMARY);
    std::string s1 = "   ├── [Process Handle] : 0x" + std::to_string(stateCopy.activePID) + " (PROCESS_ALL_ACCESS)";
    TextOutA(hdc, 35, 375, s1.c_str(), (int)s1.length());

    std::string s2 = "   ├── [Thread Handles] : " + std::to_string(stateCopy.metrics.threadCount) + " Aktif İş Parçacığı (Toolhelp32)";
    TextOutA(hdc, 35, 395, s2.c_str(), (int)s2.length());

    std::string s3 = "   ├── [Pipes (STDOUT/STDERR)] : Win32 Anonymous Pipe (ShellCore Miras)";
    TextOutA(hdc, 35, 415, s3.c_str(), (int)s3.length());

    std::string s4 = "   └── [Job Object] : " + std::string(stateCopy.sandboxEnabled ? "Bağlı (Kısıtlamalar Etkin)" : "Yok (Doğrudan Yürütme)");
    SetTextColor(hdc, stateCopy.sandboxEnabled ? Theme::ACCENT_GREEN : Theme::TEXT_MUTED);
    TextOutA(hdc, 35, 435, s4.c_str(), (int)s4.length());

    // ─────────────────────────────────────────────────────────────────────────
    // 4. KART 3: [SANDBOX PANEL] (x: 20, y: 485, w: 440, h: 145)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(20, 485, 440, 145, "3. [SANDBOX PANEL] RAM / CPU Sınırlandırma");

    // ─────────────────────────────────────────────────────────────────────────
    // 5. KART 4: [REAL-TIME GRAPH] (x: 480, y: 85, w: 470, h: 545)
    // ─────────────────────────────────────────────────────────────────────────
    drawCard(480, 85, 470, 545, "4. [REAL-TIME GRAPH] Zaman Serisi (Son 60sn)");

    // Grafik Alanı Koordinatları
    int gx = 530;
    int gy = 145;
    int gw = 400;
    int gh = 360;

    // Grafik Arkaplanı
    RECT rGraph = { gx, gy, gx + gw, gy + gh };
    HBRUSH hGraphBg = CreateSolidBrush(RGB(18, 24, 34));
    FillRect(hdc, &rGraph, hGraphBg);
    DeleteObject(hGraphBg);

    // Yatay Izgara Çizgileri ve Y Ekseni Etiketleri (%0, %25, %50, %75, %100)
    SelectObject(hdc, hFontSub);
    HPEN hGridPen = CreatePen(PS_DOT, 1, Theme::GRID_COLOR);
    HPEN hOldGridPen = (HPEN)SelectObject(hdc, hGridPen);

    for (int step = 0; step <= 4; ++step) {
        int yLine = gy + (gh * step) / 4;
        MoveToEx(hdc, gx, yLine, NULL);
        LineTo(hdc, gx + gw, yLine);

        // CPU % Etiketi (Sol)
        int pct = 100 - step * 25;
        std::string lbl = std::to_string(pct) + "%";
        SetTextColor(hdc, Theme::TEXT_MUTED);
        TextOutA(hdc, gx - 38, yLine - 8, lbl.c_str(), (int)lbl.length());

        // RAM MB Etiketi (Sağ)
        size_t ramStepVal = (maxLimitMB * (4 - step)) / 4;
        std::string ramStepStr = std::to_string(ramStepVal) + "M";
        SetTextColor(hdc, Theme::ACCENT_GREEN);
        TextOutA(hdc, gx + gw + 5, yLine - 8, ramStepStr.c_str(), (int)ramStepStr.length());
    }
    SelectObject(hdc, hOldGridPen);
    DeleteObject(hGridPen);

    // Dikey Zaman Çizgileri (-60s, -45s, -30s, -15s, Şimdi)
    HPEN hVertGridPen = CreatePen(PS_DOT, 1, Theme::GRID_COLOR);
    HPEN hOldVertPen = (HPEN)SelectObject(hdc, hVertGridPen);
    for (int step = 0; step <= 4; ++step) {
        int xLine = gx + (gw * step) / 4;
        MoveToEx(hdc, xLine, gy, NULL);
        LineTo(hdc, xLine, gy + gh);

        int sec = -60 + step * 15;
        std::string tStr = (sec == 0) ? "0s" : (std::to_string(sec) + "s");
        SetTextColor(hdc, Theme::TEXT_MUTED);
        TextOutA(hdc, xLine - 10, gy + gh + 5, tStr.c_str(), (int)tStr.length());
    }
    SelectObject(hdc, hOldVertPen);
    DeleteObject(hVertGridPen);

    // Zaman Serisi Eğrilerini Çiz
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

    // Lejant (Graph Legend, Alt Kısım)
    int legY = gy + gh + 35;
    // CPU Lejantı
    HPEN hCpuLeg = CreatePen(PS_SOLID, 3, Theme::ACCENT_CYAN);
    HPEN hOldCpuLeg = (HPEN)SelectObject(hdc, hCpuLeg);
    MoveToEx(hdc, gx + 20, legY + 8, NULL);
    LineTo(hdc, gx + 55, legY + 8);
    SelectObject(hdc, hOldCpuLeg);
    DeleteObject(hCpuLeg);
    SetTextColor(hdc, Theme::TEXT_PRIMARY);
    TextOutA(hdc, gx + 65, legY, "CPU Kullanımı (%)", 17);

    // RAM Lejantı
    HPEN hRamLeg = CreatePen(PS_SOLID, 3, Theme::ACCENT_GREEN);
    HPEN hOldRamLeg = (HPEN)SelectObject(hdc, hRamLeg);
    MoveToEx(hdc, gx + 230, legY + 8, NULL);
    LineTo(hdc, gx + 265, legY + 8);
    SelectObject(hdc, hOldRamLeg);
    DeleteObject(hRamLeg);
    SetTextColor(hdc, Theme::TEXT_PRIMARY);
    TextOutA(hdc, gx + 275, legY, "RAM Kullanımı (MB)", 18);

    // Bellek ve GDI Temizliği
    DeleteObject(hFontTitle);
    DeleteObject(hFontSub);
    DeleteObject(hFontBold);
    DeleteObject(hFontLarge);

    // Çift tamponu pencereye kopyala
    BitBlt(hdcWindow, 0, 0, width, height, hdc, 0, 0, SRCCOPY);

    SelectObject(hdc, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdc);

    EndPaint(hWnd, &ps);
}

} // namespace SpecTer
