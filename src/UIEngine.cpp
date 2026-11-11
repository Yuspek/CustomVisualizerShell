#include "UIEngine.h"
#include <commctrl.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

// Win32 Kontrol Tanımlayıcıları
#define IDC_TRACK_RAM          1001
#define IDC_TRACK_CPU          1002
#define IDC_BTN_TOGGLE_SB      1003
#define ID_TIMER_REFRESH       2001

namespace SpecTer {

#define IDC_BTN_THEME          1004
#define WM_APP_THEME_CHANGED   (WM_APP + 41)

// ─────────────────────────────────────────────────────────────────────────────
// Erişilebilirlik Tema Sistemi (Normal + 3 Temel Renk Körlüğü)
// ─────────────────────────────────────────────────────────────────────────────

// ── Ultra-Modern Renk Paleti (Cyberpunk Slate & Neon Telemetry) ── Varsayılan
static const ThemePalette kPaletteNormal = {
    RGB(15, 20, 31),     // bgMain        : Derin uzay antrasiti (#0F141F)
    RGB(21, 27, 40),     // cardBg        : Modern kart panel arkaplanı (#151B28)
    RGB(34, 45, 66),     // cardBorder    : İnce kart çerçevesi (#222D42)
    RGB(242, 246, 255),  // textPrimary   : Parlak beyaz metin
    RGB(130, 148, 172),  // textMuted     : Muted açıklama metni
    RGB(156, 172, 196),  // textLabel     : Kart başlık metni
    RGB(0, 229, 255),    // accentPrimary : CPU / Neon Camgöbeği (#00E5FF)
    RGB(0, 230, 118),    // accentSecondary: RAM / Neon Yeşil (#00E676)
    RGB(255, 171, 0),    // accentWarning : Uyarı / Turuncu
    RGB(255, 82, 82),    // accentDanger  : Kritik / Kırmızı
    RGB(28, 38, 56),     // gridColor     : Grafik ızgara çizgisi
    RGB(10, 14, 20),     // terminalBg    : Koyu siyah terminal zemini
    RGB(0, 230, 118),    // terminalText  : Neon yeşil terminal metni
    false                // shapeMarkers
};

// ── Protanopia (Kırmızı körlüğü) ── Kırmızı/yeşil ayrımı yok → Mavi ↔ Sarı ekseni
// Kırmızı tonlar protanlarda karanlık algılandığından "kritik" rengi parlak leylak seçildi.
static const ThemePalette kPaletteProtanopia = {
    RGB(14, 18, 30),     // bgMain
    RGB(20, 26, 42),     // cardBg
    RGB(44, 56, 84),     // cardBorder (daha yüksek kontrast)
    RGB(248, 250, 255),  // textPrimary
    RGB(150, 162, 186),  // textMuted
    RGB(176, 188, 210),  // textLabel
    RGB(86, 180, 233),   // accentPrimary : Gök mavisi (Okabe-Ito #56B4E9)
    RGB(240, 228, 66),   // accentSecondary: Sarı (Okabe-Ito #F0E442)
    RGB(230, 159, 0),    // accentWarning : Turuncu (Okabe-Ito #E69F00)
    RGB(190, 150, 255),  // accentDanger  : Parlak leylak
    RGB(34, 44, 66),     // gridColor
    RGB(10, 13, 22),     // terminalBg
    RGB(240, 228, 66),   // terminalText  : Sarı
    true                 // shapeMarkers
};

// ── Deuteranopia (Yeşil körlüğü) ── Kırmızı/yeşil ayrımı yok → Mavi ↔ Turuncu ekseni
static const ThemePalette kPaletteDeuteranopia = {
    RGB(15, 18, 28),     // bgMain
    RGB(22, 26, 40),     // cardBg
    RGB(46, 54, 80),     // cardBorder
    RGB(246, 248, 255),  // textPrimary
    RGB(152, 160, 182),  // textMuted
    RGB(178, 186, 206),  // textLabel
    RGB(100, 143, 255),  // accentPrimary : Mavi (IBM #648FFF)
    RGB(255, 176, 0),    // accentSecondary: Altın turuncu (IBM #FFB000)
    RGB(254, 97, 0),     // accentWarning : Koyu turuncu (IBM #FE6100)
    RGB(220, 38, 127),   // accentDanger  : Macenta (IBM #DC267F)
    RGB(34, 42, 64),     // gridColor
    RGB(10, 12, 20),     // terminalBg
    RGB(255, 196, 64),   // terminalText  : Açık altın
    true                 // shapeMarkers
};

// ── Tritanopia (Mavi körlüğü) ── Mavi/sarı ayrımı yok → Kırmızı ↔ Camgöbeği ekseni
static const ThemePalette kPaletteTritanopia = {
    RGB(20, 18, 22),     // bgMain  (nötr, mavi tonsuz zemin)
    RGB(30, 27, 32),     // cardBg
    RGB(64, 56, 64),     // cardBorder
    RGB(255, 250, 250),  // textPrimary
    RGB(170, 160, 164),  // textMuted
    RGB(196, 186, 190),  // textLabel
    RGB(255, 94, 160),   // accentPrimary : Pembe-macenta
    RGB(0, 200, 200),    // accentSecondary: Camgöbeği / teal
    RGB(255, 180, 196),  // accentWarning : Açık pembe
    RGB(235, 40, 40),    // accentDanger  : Saf kırmızı
    RGB(46, 40, 46),     // gridColor
    RGB(14, 12, 14),     // terminalBg
    RGB(80, 230, 220),   // terminalText  : Açık camgöbeği
    true                 // shapeMarkers
};

// ── Özelleştirme Anahtarları (INI & "tema renk" komutu için) ──
struct PaletteKey {
    const char* key;
    COLORREF ThemePalette::* member;
    const char* desc;
};

static const PaletteKey kPaletteKeys[] = {
    { "bgMain",          &ThemePalette::bgMain,          "Ana pencere zemini" },
    { "cardBg",          &ThemePalette::cardBg,          "Kart zemini" },
    { "cardBorder",      &ThemePalette::cardBorder,      "Kart çerçevesi" },
    { "textPrimary",     &ThemePalette::textPrimary,     "Birincil metin" },
    { "textMuted",       &ThemePalette::textMuted,       "Soluk metin" },
    { "textLabel",       &ThemePalette::textLabel,       "Kart başlığı" },
    { "accentPrimary",   &ThemePalette::accentPrimary,   "CPU vurgusu" },
    { "accentSecondary", &ThemePalette::accentSecondary, "RAM / aktif vurgusu" },
    { "accentWarning",   &ThemePalette::accentWarning,   "Uyarı vurgusu" },
    { "accentDanger",    &ThemePalette::accentDanger,    "Kritik / kapalı vurgusu" },
    { "gridColor",       &ThemePalette::gridColor,       "Izgara & gösterge izi" },
    { "terminalBg",      &ThemePalette::terminalBg,      "Terminal zemini" },
    { "terminalText",    &ThemePalette::terminalText,    "Terminal metni" },
};

static const char* kThemeIniSections[] = { "Normal", "Protanopia", "Deuteranopia", "Tritanopia" };

static int ThemeIndex(ColorBlindTheme t) {
    int i = static_cast<int>(t);
    return (i < 0 || i >= static_cast<int>(ColorBlindTheme::Count)) ? 0 : i;
}

// İki rengi t oranında harmanlar (t=0 → a, t=1 → b)
static COLORREF BlendColor(COLORREF a, COLORREF b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto mix = [t](int x, int y) { return (BYTE)std::lround(x + (y - x) * t); };
    return RGB(mix(GetRValue(a), GetRValue(b)),
               mix(GetGValue(a), GetGValue(b)),
               mix(GetBValue(a), GetBValue(b)));
}

static std::string ColorToHex(COLORREF c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

// "#RRGGBB", "RRGGBB", "0xRRGGBB" veya "R,G,B" biçimlerini çözer
static bool ParseColor(std::string s, COLORREF& out) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch); }), s.end());
    if (s.empty()) return false;

    if (s.find(',') != std::string::npos) {
        int r = -1, g = -1, b = -1;
#if defined(_MSC_VER)
        if (sscanf_s(s.c_str(), "%d,%d,%d", &r, &g, &b) == 3 &&
#else
        if (sscanf(s.c_str(), "%d,%d,%d", &r, &g, &b) == 3 &&
#endif
            r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
            out = RGB(r, g, b);
            return true;
        }
        return false;
    }

    if (s[0] == '#') s = s.substr(1);
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.size() != 6 || !std::all_of(s.begin(), s.end(), [](unsigned char ch) { return std::isxdigit(ch); })) {
        return false;
    }
    unsigned long v = std::strtoul(s.c_str(), nullptr, 16);
    out = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

static std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    return s;
}

// Kullanıcı girdisinden tema çözümleme (kısaltmalar desteklenir)
static bool ParseThemeName(const std::string& raw, ColorBlindTheme& out) {
    std::string s = ToLowerAscii(raw);
    if (s == "normal" || s == "varsayilan" || s == "default" || s == "0") { out = ColorBlindTheme::Normal; return true; }
    if (s.rfind("prot", 0) == 0 || s == "1") { out = ColorBlindTheme::Protanopia; return true; }
    if (s.rfind("deut", 0) == 0 || s == "2") { out = ColorBlindTheme::Deuteranopia; return true; }
    if (s.rfind("trit", 0) == 0 || s == "3") { out = ColorBlindTheme::Tritanopia; return true; }
    return false;
}

ThemePalette UIEngine::defaultPalette(ColorBlindTheme theme) {
    switch (theme) {
    case ColorBlindTheme::Protanopia:   return kPaletteProtanopia;
    case ColorBlindTheme::Deuteranopia: return kPaletteDeuteranopia;
    case ColorBlindTheme::Tritanopia:   return kPaletteTritanopia;
    default:                            return kPaletteNormal;
    }
}

const char* UIEngine::themeDisplayName(ColorBlindTheme theme) {
    switch (theme) {
    case ColorBlindTheme::Protanopia:   return "Protanopi";
    case ColorBlindTheme::Deuteranopia: return "Döteranopi";
    case ColorBlindTheme::Tritanopia:   return "Tritanopi";
    default:                            return "Normal";
    }
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
    , m_hStaticBrush(CreateSolidBrush(kPaletteNormal.cardBg))
    , m_hActiveProcess(NULL)
{
    // Tüm erişilebilirlik temalarını fabrika ayarlarıyla doldur
    for (int i = 0; i < static_cast<int>(ColorBlindTheme::Count); ++i) {
        m_palettes[i] = defaultPalette(static_cast<ColorBlindTheme>(i));
    }
    m_state.processName = "SpecTer Beklemede...";
    m_state.sandboxConfig.maxMemoryMB = 2048;
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

void UIEngine::resizeDashboard(int width, int height) {
    if (m_hWnd && IsWindow(m_hWnd)) {
        SetWindowPos(m_hWnd, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
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

    // Kayıtlı kullanıcı temalarını (varsa) yükle: exe yanındaki specter_themes.ini
    loadThemesFromFile();
    {
        ThemePalette P = activePalette();
        std::lock_guard<std::mutex> lock(m_themeMutex);
        if (m_hStaticBrush) DeleteObject(m_hStaticBrush);
        if (m_hTermBrush) DeleteObject(m_hTermBrush);
        m_hStaticBrush = CreateSolidBrush(P.cardBg);
        m_hTermBrush = CreateSolidBrush(P.terminalBg);
    }

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

    // Pencere Boyutu: 1400 x 800 (Oran: 7:4 / 1.75 Sabit En-Boy Oranı)
    const int baseClientW = 1400;
    const int baseClientH = 800;

    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    DWORD dwExStyle = WS_EX_APPWINDOW;

    RECT rcReq = { 0, 0, baseClientW, baseClientH };
    AdjustWindowRectEx(&rcReq, dwStyle, FALSE, dwExStyle);
    int winWidth = rcReq.right - rcReq.left;
    int winHeight = rcReq.bottom - rcReq.top;

    int posX = (GetSystemMetrics(SM_CXSCREEN) - winWidth) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winHeight) / 2;
    if (posX < 0) posX = 10;
    if (posY < 0) posY = 10;

    HWND hWnd = CreateWindowExA(
        dwExStyle,
        CLASS_NAME,
        "SpecTer | Kernel Observability & Sandboxed CLI/GUI Terminal",
        dwStyle,
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

    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    updateLayout(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);

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
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = reinterpret_cast<LPMINMAXINFO>(lParam);
        if (lpMMI) {
            RECT rcMin = { 0, 0, 980, 560 }; // 1400*0.7 x 800*0.7
            DWORD style = (DWORD)GetWindowLongA(hWnd, GWL_STYLE);
            DWORD exStyle = (DWORD)GetWindowLongA(hWnd, GWL_EXSTYLE);
            AdjustWindowRectEx(&rcMin, style, FALSE, exStyle);
            lpMMI->ptMinTrackSize.x = rcMin.right - rcMin.left;
            lpMMI->ptMinTrackSize.y = rcMin.bottom - rcMin.top;
        }
        return 0;
    }

    case WM_SIZING: {
        RECT* prc = reinterpret_cast<RECT*>(lParam);
        if (!prc) break;

        RECT rcDummy = { 0, 0, 1400, 800 };
        DWORD style = (DWORD)GetWindowLongA(hWnd, GWL_STYLE);
        DWORD exStyle = (DWORD)GetWindowLongA(hWnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&rcDummy, style, FALSE, exStyle);
        int ncW = (rcDummy.right - rcDummy.left) - 1400;
        int ncH = (rcDummy.bottom - rcDummy.top) - 800;

        int curW = (prc->right - prc->left) - ncW;
        int curH = (prc->bottom - prc->top) - ncH;
        if (curW < 100) curW = 100;
        if (curH < 100) curH = 100;

        const double TARGET_RATIO = 1400.0 / 800.0;

        switch (wParam) {
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
            curH = static_cast<int>(curW / TARGET_RATIO);
            prc->bottom = prc->top + curH + ncH;
            break;
        case WMSZ_TOP:
        case WMSZ_BOTTOM:
            curW = static_cast<int>(curH * TARGET_RATIO);
            prc->right = prc->left + curW + ncW;
            break;
        case WMSZ_TOPLEFT:
            curW = static_cast<int>(curH * TARGET_RATIO);
            prc->left = prc->right - curW - ncW;
            break;
        case WMSZ_TOPRIGHT:
            curW = static_cast<int>(curH * TARGET_RATIO);
            prc->right = prc->left + curW + ncW;
            break;
        case WMSZ_BOTTOMLEFT:
            curW = static_cast<int>(curH * TARGET_RATIO);
            prc->left = prc->right - curW - ncW;
            break;
        case WMSZ_BOTTOMRIGHT:
        default:
            curW = static_cast<int>(curH * TARGET_RATIO);
            prc->right = prc->left + curW + ncW;
            break;
        }
        return TRUE;
    }

    case WM_SIZE:
        if (pThis && wParam != SIZE_MINIMIZED) {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            pThis->updateLayout(cx, cy);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;

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

            // Sandbox Limit Aşımı Tespiti ve Terminal Uyarısı (Fikir 3)
            {
                std::lock_guard<std::mutex> lock(pThis->m_stateMutex);
                bool isRamBreach = pThis->m_state.sandboxEnabled &&
                                   pThis->m_state.sandboxConfig.maxMemoryMB > 0 &&
                                   pThis->m_state.metrics.workingSetSizeMB >= pThis->m_state.sandboxConfig.maxMemoryMB;
                bool isCpuBreach = pThis->m_state.sandboxEnabled &&
                                   pThis->m_state.sandboxConfig.maxCpuPercent > 0 &&
                                   pThis->m_state.metrics.cpuUsagePercent >= static_cast<double>(pThis->m_state.sandboxConfig.maxCpuPercent);
                bool anyBreach = isRamBreach || isCpuBreach;

                if (anyBreach && !pThis->m_lastBreachState) {
                    pThis->m_lastBreachState = true;
                    std::string warn = "\r\n[⚠️ GÜVENLİK UYARISI]: Süreç sandbox kaynak sınırına dayandı!";
                    if (isRamBreach) {
                        warn += " (RAM Sınırı: " + std::to_string(pThis->m_state.sandboxConfig.maxMemoryMB) + " MB)";
                    }
                    if (isCpuBreach) {
                        warn += " (CPU Sınırı: %" + std::to_string(pThis->m_state.sandboxConfig.maxCpuPercent) + ")";
                    }
                    warn += "\r\n";
                    pThis->printTerminalText(warn);
                    pThis->printPrompt();
                } else if (!anyBreach) {
                    pThis->m_lastBreachState = false;
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
        const ThemePalette P = pThis ? pThis->activePalette() : kPaletteNormal;
        if (pThis && hCtrl == pThis->m_hTerminalEdit) {
            SetTextColor(hdcStatic, P.terminalText); // Tema terminal metni
            SetBkColor(hdcStatic, P.terminalBg);     // Tema terminal zemini
            if (!pThis->m_hTermBrush) {
                pThis->m_hTermBrush = CreateSolidBrush(P.terminalBg);
            }
            return (LRESULT)pThis->m_hTermBrush;
        }
        SetTextColor(hdcStatic, P.textPrimary);
        SetBkColor(hdcStatic, P.cardBg);
        if (pThis) {
            if (!pThis->m_hStaticBrush) {
                pThis->m_hStaticBrush = CreateSolidBrush(P.cardBg);
            }
            return (LRESULT)pThis->m_hStaticBrush;
        }
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_KEYDOWN:
        // F2: Erişilebilirlik temaları arasında geçiş
        if (wParam == VK_F2 && pThis) {
            pThis->cycleTheme();
            return 0;
        }
        break;

    case WM_APP_THEME_CHANGED:
        // Tema değişimi her zaman GUI thread'inde uygulanır (GDI fırçaları thread-güvenli değildir)
        if (pThis) {
            const ThemePalette P = pThis->activePalette();
            HBRUSH hOldStatic = pThis->m_hStaticBrush;
            HBRUSH hOldTerm = pThis->m_hTermBrush;
            pThis->m_hStaticBrush = CreateSolidBrush(P.cardBg);
            pThis->m_hTermBrush = CreateSolidBrush(P.terminalBg);
            // Trackbar'lar arka plan fırçasını önbelleğe alır; yeniden sorgulamaları için zorla
            for (HWND hTrack : { pThis->m_hTrackRam, pThis->m_hTrackCpu }) {
                if (hTrack && IsWindow(hTrack)) {
                    SendMessage(hTrack, WM_SYSCOLORCHANGE, 0, 0);
                    LRESULT pos = SendMessage(hTrack, TBM_GETPOS, 0, 0);
                    SendMessage(hTrack, TBM_SETPOS, TRUE, pos);
                }
            }
            RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            if (hOldStatic) DeleteObject(hOldStatic);
            if (hOldTerm) DeleteObject(hOldTerm);
            return 0;
        }
        break;

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
    SendMessage(m_hTrackRam, TBM_SETPOS, TRUE, 2048);

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

    // 4. Başlık Barı Erişilebilirlik Tema Seçici (Tıkla / F2 / Ctrl+T ile döngü)
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    m_hBtnTheme = CreateWindowA("BUTTON", "TEMA",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        rcClient.right - 404, 8, 192, 28,
        hWnd, (HMENU)IDC_BTN_THEME, hInst, NULL);
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

    if (id == IDC_BTN_THEME) {
        cycleTheme();
        // Klavye odağını terminale geri ver
        if (m_hTerminalEdit && IsWindow(m_hTerminalEdit)) {
            SetFocus(m_hTerminalEdit);
        }
        return;
    }

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
    int btnFontSize = std::clamp(static_cast<int>(15 * m_scale), 9, 24);
    HFONT hBtnFont = CreateFontA(btnFontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hBtnFont);
    const ThemePalette P = activePalette();

    if (pDIS->CtlID == IDC_BTN_THEME) {
        // Başlık barı zemini ile köşeleri temizle
        HBRUSH hClear = CreateSolidBrush(P.bgMain);
        FillRect(hdc, &rc, hClear);
        DeleteObject(hClear);

        bool pressed = (pDIS->itemState & ODS_SELECTED) != 0;
        HBRUSH hBrush = CreateSolidBrush(pressed ? BlendColor(P.cardBg, P.accentPrimary, 0.25) : P.cardBg);
        HPEN hPen = CreatePen(PS_SOLID, 1, P.accentPrimary);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        int rRad = (std::max)(6, static_cast<int>(12 * m_scale));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, rRad, rRad);
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        // Palet önizleme renk örnekleri (CPU ● / RAM ■ / Uyarı / Kritik)
        const COLORREF swatches[4] = { P.accentPrimary, P.accentSecondary, P.accentWarning, P.accentDanger };
        int swatchSize = std::clamp(static_cast<int>(8 * m_scale), 6, 16);
        int sx = rc.left + static_cast<int>(10 * m_scale);
        int sy = rc.top + (rc.bottom - rc.top - swatchSize) / 2;
        for (int i = 0; i < 4; ++i) {
            HBRUSH hSw = CreateSolidBrush(swatches[i]);
            HPEN hSwPen = CreatePen(PS_SOLID, 1, swatches[i]);
            SelectObject(hdc, hSw);
            SelectObject(hdc, hSwPen);
            if (i == 0) Ellipse(hdc, sx, sy, sx + swatchSize, sy + swatchSize);
            else Rectangle(hdc, sx, sy, sx + swatchSize, sy + swatchSize);
            SelectObject(hdc, hOldBrush);
            SelectObject(hdc, hOldPen);
            DeleteObject(hSw);
            DeleteObject(hSwPen);
            sx += swatchSize + static_cast<int>(3 * m_scale);
        }

        RECT rText = rc;
        rText.left = sx + static_cast<int>(4 * m_scale);
        rText.right -= static_cast<int>(8 * m_scale);
        std::string label = std::string("TEMA: ") + themeDisplayName(getTheme());
        DrawTextUtf8(hdc, label, &rText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, P.textPrimary);
    }

    if (pDIS->CtlID == IDC_BTN_TOGGLE_SB) {
        bool sbEnabled = false;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            sbEnabled = m_state.sandboxEnabled;
        }

        // Kart zemini ile köşeleri temizle
        HBRUSH hClear = CreateSolidBrush(P.cardBg);
        FillRect(hdc, &rc, hClear);
        DeleteObject(hClear);

        COLORREF bgCol = sbEnabled ? BlendColor(P.cardBg, P.accentSecondary, 0.18) : BlendColor(P.cardBg, P.cardBorder, 0.15);
        COLORREF borderCol = sbEnabled ? P.accentSecondary : BlendColor(P.cardBorder, P.textMuted, 0.25);

        HBRUSH hBrush = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 2, borderCol);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

        int pillRad = (std::max)(12, static_cast<int>(26 * m_scale));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, pillRad, pillRad);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        // Sol Metin: "SANDBOX: AKTİF" veya "SANDBOX: KAPALI"
        RECT rText = rc;
        rText.left += static_cast<int>(26 * m_scale);
        rText.right -= static_cast<int>(64 * m_scale);
        std::string text = sbEnabled ? "SANDBOX: AKTİF" : "SANDBOX: KAPALI";
        DrawTextUtf8(hdc, text, &rText, DT_LEFT | DT_VCENTER | DT_SINGLELINE, sbEnabled ? P.accentSecondary : P.textMuted);

        // Sağ Toggle Switch Kapsülü
        int switchH = std::clamp(static_cast<int>((rc.bottom - rc.top) * 0.48), 16, 36);
        int switchW = static_cast<int>(switchH * 1.9);
        int switchX = rc.right - switchW - static_cast<int>(16 * m_scale);
        int switchY = rc.top + (rc.bottom - rc.top - switchH) / 2;

        HBRUSH hSwBg = CreateSolidBrush(sbEnabled ? BlendColor(P.cardBg, P.accentSecondary, 0.32) : BlendColor(P.cardBg, P.cardBorder, 0.6));
        HPEN hSwPen = CreatePen(PS_SOLID, 1, sbEnabled ? P.accentSecondary : BlendColor(P.cardBorder, P.textMuted, 0.4));
        SelectObject(hdc, hSwBg);
        SelectObject(hdc, hSwPen);
        RoundRect(hdc, switchX, switchY, switchX + switchW, switchY + switchH, switchH, switchH);
        DeleteObject(hSwBg);
        DeleteObject(hSwPen);

        // Toggle Düğmesi (Thumb Knob)
        int knobRadius = (switchH / 2) - 3;
        int knobX = sbEnabled ? (switchX + switchW - knobRadius - 3) : (switchX + knobRadius + 3);
        int knobY = switchY + switchH / 2;

        HBRUSH hKnobBrush = CreateSolidBrush(sbEnabled ? P.accentSecondary : BlendColor(P.textMuted, P.textPrimary, 0.15));
        HPEN hKnobPen = CreatePen(PS_SOLID, 1, P.textPrimary);
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

    const ThemePalette P = activePalette();

    HBRUSH hBgBrush = CreateSolidBrush(P.bgMain);
    FillRect(hdc, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    DashboardState stateCopy;
    std::vector<MetricSample> historyCopy;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        stateCopy = m_state;
        historyCopy.assign(m_history.begin(), m_history.end());
    }

    // Dinamik koordinat ve boyut dönüştürücüler
    auto S_X = [this](int x) { return m_viewportX + static_cast<int>(x * m_scale); };
    auto S_Y = [this](int y) { return m_viewportY + static_cast<int>(y * m_scale); };
    auto S_D = [this](int d) { return (std::max)(1, static_cast<int>(d * m_scale)); };

    // Fikir 3: Görsel Sandbox Limit Aşım Kontrolleri & Flaş Zamanlaması
    bool isRamBreach = stateCopy.sandboxEnabled && stateCopy.sandboxConfig.maxMemoryMB > 0 &&
                       stateCopy.metrics.workingSetSizeMB >= stateCopy.sandboxConfig.maxMemoryMB;
    bool isCpuBreach = stateCopy.sandboxEnabled && stateCopy.sandboxConfig.maxCpuPercent > 0 &&
                       stateCopy.metrics.cpuUsagePercent >= static_cast<double>(stateCopy.sandboxConfig.maxCpuPercent);
    bool anyBreach = isRamBreach || isCpuBreach;
    bool flashTick = ((GetTickCount64() / 350) % 2 == 0);

    HFONT hFontLogo = CreateFontA(S_D(17), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontTitle = CreateFontA(S_D(13), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontSub = CreateFontA(S_D(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontBold = CreateFontA(S_D(13), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontGaugeVal = CreateFontA(S_D(34), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT hFontTree = CreateFontA(S_D(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Segoe UI");
    HFONT hFontAxis = CreateFontA(S_D(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    SetBkMode(hdc, TRANSPARENT);

    // ─────────────────────────────────────────────────────────────────────────
    // 1. ÜST BAŞLIK BARI (Header Bar, y: 0..44)
    // ─────────────────────────────────────────────────────────────────────────
    // Sol Logo: Yeşil SpecTer Amblemi
    HBRUSH hLogoBrush = CreateSolidBrush(P.accentSecondary);
    HPEN hLogoPen = CreatePen(PS_SOLID, 1, P.accentSecondary);
    SelectObject(hdc, hLogoBrush);
    SelectObject(hdc, hLogoPen);
    POINT logoPts[6] = {
        {S_X(20), S_Y(16)}, {S_X(26), S_Y(12)}, {S_X(32), S_Y(16)},
        {S_X(32), S_Y(26)}, {S_X(26), S_Y(30)}, {S_X(20), S_Y(26)}
    };
    Polygon(hdc, logoPts, 6);
    DeleteObject(hLogoBrush);
    DeleteObject(hLogoPen);

    SelectObject(hdc, hFontLogo);
    TextOutUtf8(hdc, S_X(38), S_Y(11), "SpecTer", P.textPrimary);

    SelectObject(hdc, hFontSub);
    TextOutUtf8(hdc, S_X(110), S_Y(14), ":: Kernel Observability & Sandboxed CLI/GUI Terminal", P.textMuted);

    // Ortada Aktif Süreç Rozeti
    std::string procText = "⚡ Süreç: " + (stateCopy.processName.empty() ? "Beklemede..." : stateCopy.processName);
    if (stateCopy.activePID > 0) {
        procText += " [PID: " + std::to_string(stateCopy.activePID) + "]";
    }
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, S_X(590), S_Y(13), procText, P.textPrimary);

    // Sağ Üst Sandbox Durum Rozeti (Limit aşımında yanıp söner)
    RECT rBadge = { S_X(1200), S_Y(8), S_X(1380), S_Y(36) };
    COLORREF badgeBorderCol;
    COLORREF badgeBgCol;
    std::string badgeStr;

    if (stateCopy.sandboxEnabled) {
        if (anyBreach) {
            badgeBorderCol = flashTick ? P.accentDanger : BlendColor(P.accentDanger, P.textPrimary, 0.4);
            badgeBgCol = BlendColor(P.bgMain, P.accentDanger, flashTick ? 0.35 : 0.18);
            badgeStr = flashTick ? "⚠️ LIMIT AŞIMI (DEVREDE)" : "🛡️ KORUMA AKTİF";
        } else {
            badgeBorderCol = P.accentSecondary;
            badgeBgCol = BlendColor(P.bgMain, P.accentSecondary, 0.2);
            badgeStr = "🛡️ SANDBOX: AKTİF";
        }
    } else {
        badgeBorderCol = P.accentDanger;
        badgeBgCol = BlendColor(P.bgMain, P.accentDanger, 0.16);
        badgeStr = "⚪ SANDBOX: KAPALI";
    }

    HBRUSH hBadgeBrush = CreateSolidBrush(badgeBgCol);
    HPEN hBadgePen = CreatePen(PS_SOLID, 1, badgeBorderCol);
    SelectObject(hdc, hBadgeBrush);
    SelectObject(hdc, hBadgePen);
    RoundRect(hdc, rBadge.left, rBadge.top, rBadge.right, rBadge.bottom, S_D(12), S_D(12));
    DeleteObject(hBadgeBrush);
    DeleteObject(hBadgePen);

    COLORREF badgeTextCol = (stateCopy.sandboxEnabled && anyBreach) ? P.accentDanger :
                            (stateCopy.sandboxEnabled ? P.accentSecondary : BlendColor(P.accentDanger, P.textPrimary, 0.25));
    DrawTextUtf8(hdc, badgeStr, &rBadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE, badgeTextCol);

    // ─────────────────────────────────────────────────────────────────────────
    // 2. SOL PANEL: GÖMÜLÜ TERMİNAL CARD (x: 20, y: 56, w: 550, h: 694)
    // ─────────────────────────────────────────────────────────────────────────
    RECT rLeftCard = { S_X(20), S_Y(56), S_X(570), S_Y(750) };
    HBRUSH hLeftCardBg = CreateSolidBrush(P.terminalBg);
    HPEN hLeftCardPen = CreatePen(PS_SOLID, 1, P.cardBorder);
    SelectObject(hdc, hLeftCardBg);
    SelectObject(hdc, hLeftCardPen);
    RoundRect(hdc, rLeftCard.left, rLeftCard.top, rLeftCard.right, rLeftCard.bottom, S_D(16), S_D(16));
    DeleteObject(hLeftCardBg);
    DeleteObject(hLeftCardPen);

    // Terminal Başlık Çubuğu
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, S_X(34), S_Y(68), "📟 GÖMÜLÜ TERMİNAL (ShellCore REPL v1.0)", P.accentSecondary);

    HPEN hTermSep = CreatePen(PS_SOLID, 1, P.cardBorder);
    SelectObject(hdc, hTermSep);
    MoveToEx(hdc, S_X(26), S_Y(88), NULL);
    LineTo(hdc, S_X(564), S_Y(88));
    DeleteObject(hTermSep);

    // ─────────────────────────────────────────────────────────────────────────
    // SAĞ PANEL 2x2 GRID KART ÇİZİM YARDIMCISI (specter_dashboard.jpg 1:1)
    // ─────────────────────────────────────────────────────────────────────────
    auto drawDashboardCard = [&](int x, int y, int w, int h, const std::string& title, bool alertCard = false) {
        COLORREF bCol = (alertCard && flashTick) ? P.accentDanger : P.cardBorder;
        HPEN hCardPen = CreatePen(PS_SOLID, alertCard ? 2 : 1, bCol);
        HBRUSH hCardBrush = CreateSolidBrush(P.cardBg);
        HPEN hOldP = (HPEN)SelectObject(hdc, hCardPen);
        HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hCardBrush);

        RoundRect(hdc, x, y, x + w, y + h, S_D(16), S_D(16));

        SelectObject(hdc, hOldB);
        SelectObject(hdc, hOldP);
        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);

        // Başlık
        SelectObject(hdc, hFontTitle);
        TextOutUtf8(hdc, x + S_D(20), y + S_D(16), title, alertCard ? P.accentDanger : P.textLabel);

        // Sağ üst köşe "···" simgesi
        SelectObject(hdc, hFontLogo);
        TextOutUtf8(hdc, x + w - S_D(36), y + S_D(10), "···", BlendColor(P.textMuted, P.cardBg, 0.4));
    };

    const int col1X = S_X(588);
    const int col2X = S_X(990);
    const int cardW = S_D(382);
    const int row1Y = S_Y(56);
    const int row2Y = S_Y(413);
    const int cardH = S_D(337);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 1 (Col 1, Row 1): "CPU USAGE"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col1X, row1Y, cardW, cardH, "CPU USAGE", isCpuBreach || isRamBreach);

    double cpuVal = std::clamp(stateCopy.metrics.cpuUsagePercent, 0.0, 100.0);
    int gaugeCenterX = col1X + cardW / 2;
    int gaugeCenterY = row1Y + S_D(135);
    int gaugeRadius = S_D(60);

    // 270 Derecelik Açık Dairesel Gösterge (135° ile 405° arası)
    // 1) Arka plan koyu iz
    HPEN hTrackPen = CreatePen(PS_SOLID, S_D(10), P.gridColor);
    HPEN hOldTrack = (HPEN)SelectObject(hdc, hTrackPen);
    int arcD = static_cast<int>(gaugeRadius * 0.7071);
    Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
             gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
             gaugeCenterX - arcD, gaugeCenterY + arcD,
             gaugeCenterX + arcD, gaugeCenterY + arcD);
    SelectObject(hdc, hOldTrack);
    DeleteObject(hTrackPen);

    // 2) Aktif Neon Camgöbeği Yay (Limit aşımında kırmızı flaş yapar)
    if (cpuVal > 0.5) {
        double startAngle = 135.0 * (3.14159265 / 180.0);
        double sweepAngle = (cpuVal / 100.0) * 270.0 * (3.14159265 / 180.0);
        double endAngle = startAngle + sweepAngle;

        int sx = gaugeCenterX - static_cast<int>(gaugeRadius * std::cos(startAngle));
        int sy = gaugeCenterY + static_cast<int>(gaugeRadius * std::sin(startAngle));
        int ex = gaugeCenterX - static_cast<int>(gaugeRadius * std::cos(endAngle));
        int ey = gaugeCenterY + static_cast<int>(gaugeRadius * std::sin(endAngle));

        COLORREF arcColor = isCpuBreach ? (flashTick ? P.accentDanger : RGB(255, 60, 60)) : P.accentPrimary;

        // Dış neon ışıma
        HPEN hGlowPen = CreatePen(PS_SOLID, S_D(14), BlendColor(P.cardBg, arcColor, 0.45));
        SelectObject(hdc, hGlowPen);
        Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
                 gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
                 sx, sy, ex, ey);
        DeleteObject(hGlowPen);

        // Ana parlak yay
        HPEN hCpuPen = CreatePen(PS_SOLID, S_D(9), arcColor);
        SelectObject(hdc, hCpuPen);
        Arc(hdc, gaugeCenterX - gaugeRadius, gaugeCenterY - gaugeRadius,
                 gaugeCenterX + gaugeRadius, gaugeCenterY + gaugeRadius,
                 sx, sy, ex, ey);
        DeleteObject(hCpuPen);
    }

    // Gösterge Merkezindeki Büyük Yüzde Metni (Örn: 38%)
    SelectObject(hdc, hFontGaugeVal);
    std::string sCpu = std::to_string(static_cast<int>(std::round(cpuVal))) + "%";
    RECT rCpuVal = { gaugeCenterX - S_D(60), gaugeCenterY - S_D(20), gaugeCenterX + S_D(60), gaugeCenterY + S_D(20) };
    DrawTextUtf8(hdc, sCpu, &rCpuVal, DT_CENTER | DT_VCENTER | DT_SINGLELINE, isCpuBreach ? (flashTick ? P.accentDanger : P.textPrimary) : P.textPrimary);

    if (isCpuBreach) {
        SelectObject(hdc, hFontSub);
        RECT rCpuAlert = { gaugeCenterX - S_D(100), gaugeCenterY + S_D(24), gaugeCenterX + S_D(100), gaugeCenterY + S_D(44) };
        DrawTextUtf8(hdc, "⚠️ CPU LİMİTİ AŞILDI!", &rCpuAlert, DT_CENTER | DT_VCENTER | DT_SINGLELINE, P.accentDanger);
    }

    // Alt Bellek (MEMORY) İlerleme Çubuğu ve Değerleri
    SelectObject(hdc, hFontBold);
    TextOutUtf8(hdc, col1X + S_D(26), row1Y + S_D(242), "MEMORY", isRamBreach ? P.accentDanger : P.textMuted);

    size_t maxLimitMB = (stateCopy.sandboxConfig.maxMemoryMB > 0) ? stateCopy.sandboxConfig.maxMemoryMB : 512;
    std::string ramValText = std::to_string(stateCopy.metrics.workingSetSizeMB) + " MB / " + std::to_string(maxLimitMB) + " MB";
    if (isRamBreach) ramValText += " ⚠️ AŞILDI";
    RECT rRamText = { col1X + S_D(140), row1Y + S_D(242), col1X + cardW - S_D(26), row1Y + S_D(260) };
    DrawTextUtf8(hdc, ramValText, &rRamText, DT_RIGHT | DT_SINGLELINE, isRamBreach ? P.accentDanger : BlendColor(P.textMuted, P.textPrimary, 0.6));

    // Yuvarlatılmış İlerleme Çubuğu Yuvası
    RECT rRamBg = { col1X + S_D(26), row1Y + S_D(270), col1X + cardW - S_D(26), row1Y + S_D(282) };
    HBRUSH hRamBgBrush = CreateSolidBrush(P.gridColor);
    SelectObject(hdc, hRamBgBrush);
    RoundRect(hdc, rRamBg.left, rRamBg.top, rRamBg.right, rRamBg.bottom, S_D(8), S_D(8));
    DeleteObject(hRamBgBrush);

    double ramRatio = std::clamp(static_cast<double>(stateCopy.metrics.workingSetSizeMB) / static_cast<double>(maxLimitMB), 0.0, 1.0);
    int barW = static_cast<int>((rRamBg.right - rRamBg.left) * ramRatio);
    if (barW > 6) {
        RECT rRamFill = { rRamBg.left, rRamBg.top, rRamBg.left + barW, rRamBg.bottom };
        COLORREF ramColor = isRamBreach ? (flashTick ? P.accentDanger : RGB(255, 60, 60)) : P.accentSecondary;
        HBRUSH hRamFillBrush = CreateSolidBrush(ramColor);
        SelectObject(hdc, hRamFillBrush);
        RoundRect(hdc, rRamFill.left, rRamFill.top, rRamFill.right, rRamFill.bottom, S_D(8), S_D(8));
        DeleteObject(hRamFillBrush);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // KART 2 (Col 2, Row 1): "KERNEL HANDLE TREE"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col2X, row1Y, cardW, cardH, "KERNEL HANDLE TREE");

    SelectObject(hdc, hFontTree);
    int treeX = col2X + S_D(24);
    int treeY = row1Y + S_D(54);
    int lineH = S_D(29);

    // Line 1: v 📁 Process (PID: ...)
    DrawChevron(hdc, treeX, treeY + S_D(3), true, P.textMuted);
    DrawFolderIcon(hdc, treeX + S_D(14), treeY + S_D(1), P.accentPrimary);
    std::string treeP = "Process (" + std::to_string(stateCopy.activePID > 0 ? 3 : 1) + ")";
    TextOutUtf8(hdc, treeX + S_D(34), treeY, treeP, BlendColor(P.textLabel, P.textPrimary, 0.7));
    treeY += lineH;

    // Line 2:   > 📁 Thread (1)
    DrawChevron(hdc, treeX + S_D(16), treeY + S_D(3), false, P.textMuted);
    DrawFolderIcon(hdc, treeX + S_D(30), treeY + S_D(1), BlendColor(P.textMuted, P.textPrimary, 0.4));
    TextOutUtf8(hdc, treeX + S_D(50), treeY, "Thread (1)", P.textMuted);
    treeY += lineH;

    // Line 3:   v 📁 Thread (2)
    DrawChevron(hdc, treeX + S_D(16), treeY + S_D(3), true, P.textMuted);
    DrawFolderIcon(hdc, treeX + S_D(30), treeY + S_D(1), BlendColor(P.textMuted, P.textPrimary, 0.4));
    std::string threadNode = "Thread (" + std::to_string(std::max<DWORD>(2, stateCopy.metrics.threadCount)) + ")";
    TextOutUtf8(hdc, treeX + S_D(50), treeY, threadNode, BlendColor(P.textLabel, P.textPrimary, 0.7));
    treeY += lineH;

    // Line 4:       📄 Win32 A00 (2)
    DrawDocIcon(hdc, treeX + S_D(52), treeY + S_D(1), P.textMuted);
    TextOutUtf8(hdc, treeX + S_D(70), treeY, "Win32 A00 (2)", P.textMuted);
    treeY += lineH;

    // Line 5:       📄 Win32 CPU (1)
    DrawDocIcon(hdc, treeX + S_D(52), treeY + S_D(1), P.textMuted);
    TextOutUtf8(hdc, treeX + S_D(70), treeY, "Win32 CPU (1)", P.textMuted);
    treeY += lineH;

    // Line 6:   > 📁 Win32 Handle Trees (3)
    DrawChevron(hdc, treeX + S_D(16), treeY + S_D(3), false, P.textMuted);
    DrawFolderIcon(hdc, treeX + S_D(30), treeY + S_D(1), BlendColor(P.textMuted, P.textPrimary, 0.4));
    std::string handleTrees = "Win32 Handle Trees (" + std::to_string(stateCopy.metrics.openHandleCount > 0 ? stateCopy.metrics.openHandleCount : 3) + ")";
    TextOutUtf8(hdc, treeX + S_D(50), treeY, handleTrees, P.textMuted);
    treeY += lineH;

    // Line 7:   > 📁 Anonymous Pipes (2)
    DrawChevron(hdc, treeX + S_D(16), treeY + S_D(3), false, P.textMuted);
    DrawFolderIcon(hdc, treeX + S_D(30), treeY + S_D(1), P.accentWarning);
    TextOutUtf8(hdc, treeX + S_D(50), treeY, "Anonymous Pipes (2)", P.textMuted);
    treeY += lineH;

    // Line 8:   > 📁 Win32 Handle (3)
    DrawChevron(hdc, treeX + S_D(16), treeY + S_D(3), false, P.textMuted);
    DrawFolderIcon(hdc, treeX + S_D(30), treeY + S_D(1), BlendColor(P.textMuted, P.textPrimary, 0.4));
    TextOutUtf8(hdc, treeX + S_D(50), treeY, "Win32 Handle (3)", P.textMuted);
    treeY += lineH;

    // Line 9:   > 📁 Anonymous Pipes (1)
    DrawChevron(hdc, treeX + S_D(16), treeY + S_D(3), false, P.textMuted);
    DrawFolderIcon(hdc, treeX + S_D(30), treeY + S_D(1), P.accentWarning);
    TextOutUtf8(hdc, treeX + S_D(50), treeY, "Anonymous Pipes (1)", P.textMuted);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 3 (Col 1, Row 2): "SANDBOXING"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col1X, row2Y, cardW, cardH, "SANDBOXING", anyBreach);

    SelectObject(hdc, hFontBold);
    std::string ramTitle = isRamBreach ? "RAM ⚠️ LİMİTTE!" : "RAM / 2048 MB";
    TextOutUtf8(hdc, col1X + S_D(26), row2Y + S_D(44), ramTitle, isRamBreach ? P.accentDanger : P.textPrimary);
    std::string ramLimitVal = std::to_string(stateCopy.sandboxConfig.maxMemoryMB) + " MB LIMIT";
    RECT rRamLim = { col1X + S_D(160), row2Y + S_D(44), col1X + cardW - S_D(26), row2Y + S_D(62) };
    DrawTextUtf8(hdc, ramLimitVal, &rRamLim, DT_RIGHT | DT_SINGLELINE, isRamBreach ? P.accentDanger : P.textMuted);

    std::string cpuTitle = isCpuBreach ? "CPU ⚠️ LİMİTTE!" : "CPU";
    TextOutUtf8(hdc, col1X + S_D(26), row2Y + S_D(122), cpuTitle, isCpuBreach ? P.accentDanger : P.textPrimary);
    std::string cpuLimitVal = "%" + std::to_string(stateCopy.sandboxConfig.maxCpuPercent) + " LIMIT";
    RECT rCpuLim = { col1X + S_D(160), row2Y + S_D(122), col1X + cardW - S_D(26), row2Y + S_D(140) };
    DrawTextUtf8(hdc, cpuLimitVal, &rCpuLim, DT_RIGHT | DT_SINGLELINE, isCpuBreach ? P.accentDanger : P.textMuted);

    // ─────────────────────────────────────────────────────────────────────────
    // KART 4 (Col 2, Row 2): "TIME GRAPH"
    // ─────────────────────────────────────────────────────────────────────────
    drawDashboardCard(col2X, row2Y, cardW, cardH, "TIME GRAPH", anyBreach);

    int gx = col2X + S_D(46);
    int gy = row2Y + S_D(48);
    int gw = cardW - S_D(92);
    int gh = S_D(200);

    // Eksen Başlıkları (Sol: CPU %, Sağ: RAM MB)
    SelectObject(hdc, hFontAxis);
    RECT rCpuHead = { col2X + S_D(2), gy - S_D(16), gx - S_D(4), gy };
    DrawTextUtf8(hdc, "CPU %", &rCpuHead, DT_RIGHT | DT_SINGLELINE, P.accentPrimary);

    RECT rRamHead = { gx + gw + S_D(4), gy - S_D(16), col2X + cardW - S_D(4), gy };
    DrawTextUtf8(hdc, "RAM", &rRamHead, DT_LEFT | DT_SINGLELINE, P.accentSecondary);

    // Grafik Zemin Dikdörtgeni
    RECT rGraph = { gx, gy, gx + gw, gy + gh };
    HBRUSH hGraphBg = CreateSolidBrush(BlendColor(P.cardBg, P.bgMain, 0.6));
    HPEN hGraphBorder = CreatePen(PS_SOLID, 1, anyBreach ? BlendColor(P.cardBorder, P.accentDanger, 0.6) : P.cardBorder);
    SelectObject(hdc, hGraphBg);
    SelectObject(hdc, hGraphBorder);
    RoundRect(hdc, rGraph.left, rGraph.top, rGraph.right, rGraph.bottom, S_D(10), S_D(10));
    DeleteObject(hGraphBg);
    DeleteObject(hGraphBorder);

    // Dotted Izgara Çizgileri ve Yan Eksen Değerleri (5 Adım: %100, %75, %50, %25, %0)
    HPEN hGridPen = CreatePen(PS_DOT, 1, BlendColor(P.gridColor, P.cardBorder, 0.5));
    HPEN hOldGridPen = (HPEN)SelectObject(hdc, hGridPen);

    for (int step = 0; step <= 4; ++step) {
        int yLine = gy + (gh * step) / 4;
        MoveToEx(hdc, gx + 2, yLine, NULL);
        LineTo(hdc, gx + gw - 2, yLine);

        // Sol Eksen Değeri: CPU % (100%, 75%, 50%, 25%, 0%)
        int cpuPct = 100 - step * 25;
        std::string cpuValStr = "%" + std::to_string(cpuPct);
        RECT rCpuScale = { col2X + S_D(2), yLine - S_D(7), gx - S_D(5), yLine + S_D(9) };
        DrawTextUtf8(hdc, cpuValStr, &rCpuScale, DT_RIGHT | DT_SINGLELINE | DT_VCENTER, BlendColor(P.accentPrimary, P.textPrimary, 0.35));

        // Sağ Eksen Değeri: RAM MB (maxLimitMB ... 0M)
        size_t ramStepMB = (maxLimitMB * (4 - step)) / 4;
        std::string ramValStr = std::to_string(ramStepMB) + "M";
        RECT rRamScale = { gx + gw + S_D(5), yLine - S_D(7), col2X + cardW - S_D(4), yLine + S_D(9) };
        DrawTextUtf8(hdc, ramValStr, &rRamScale, DT_LEFT | DT_SINGLELINE | DT_VCENTER, BlendColor(P.accentSecondary, P.textPrimary, 0.35));
    }

    // Dikey Izgara Çizgileri ve Zaman İşaretleri (-60s ... 0s)
    const char* timeLabels[5] = { "-60s", "-45s", "-30s", "-15s", "0s" };
    for (int step = 0; step <= 4; ++step) {
        int xLine = gx + (gw * step) / 4;
        MoveToEx(hdc, xLine, gy + 2, NULL);
        LineTo(hdc, xLine, gy + gh - 2);

        RECT rTime = { xLine - S_D(18), gy + gh + S_D(4), xLine + S_D(18), gy + gh + S_D(18) };
        DrawTextUtf8(hdc, timeLabels[step], &rTime, DT_CENTER | DT_SINGLELINE, P.textMuted);
    }
    SelectObject(hdc, hOldGridPen);
    DeleteObject(hGridPen);

    // Eğrilerin Çizimi
    if (historyCopy.size() >= 2) {
        // RAM Yumuşak Alan Dolgusu
        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double rVal = std::clamp(static_cast<double>(historyCopy[i].ramMB) / static_cast<double>(maxLimitMB), 0.0, 1.0);
            int ptX = gx + static_cast<int>((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = (gy + gh - 2) - static_cast<int>((gh - 4) * rVal);

            HPEN hFillPen = CreatePen(PS_SOLID, 1, BlendColor(BlendColor(P.cardBg, P.bgMain, 0.6), P.accentSecondary, 0.2));
            HPEN hOldFP = (HPEN)SelectObject(hdc, hFillPen);
            MoveToEx(hdc, ptX, ptY, NULL);
            LineTo(hdc, ptX, gy + gh - 2);
            SelectObject(hdc, hOldFP);
            DeleteObject(hFillPen);
        }

        // RAM Eğrisi (Neon Yeşil Dalga)
        HPEN hRamGraphPen = CreatePen(PS_SOLID, 2, P.accentSecondary);
        HPEN hOldRamGraph = (HPEN)SelectObject(hdc, hRamGraphPen);
        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double rVal = std::clamp(static_cast<double>(historyCopy[i].ramMB) / static_cast<double>(maxLimitMB), 0.0, 1.0);
            int ptX = gx + static_cast<int>((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = (gy + gh - 2) - static_cast<int>((gh - 4) * rVal);
            if (i == 0) MoveToEx(hdc, ptX, ptY, NULL);
            else LineTo(hdc, ptX, ptY);
        }
        SelectObject(hdc, hOldRamGraph);
        DeleteObject(hRamGraphPen);

        // CPU Eğrisi (Neon Camgöbeği Dalga)
        HPEN hCpuGraphPen = CreatePen(PS_SOLID, 2, P.accentPrimary);
        HPEN hOldCpuGraph = (HPEN)SelectObject(hdc, hCpuGraphPen);
        for (size_t i = 0; i < historyCopy.size(); ++i) {
            double cVal = std::clamp(historyCopy[i].cpuUsagePercent, 0.0, 100.0);
            int ptX = gx + static_cast<int>((gw * i) / (MAX_HISTORY_POINTS - 1));
            int ptY = (gy + gh - 2) - static_cast<int>((gh - 4) * (cVal / 100.0));
            if (i == 0) MoveToEx(hdc, ptX, ptY, NULL);
            else LineTo(hdc, ptX, ptY);
        }
        SelectObject(hdc, hOldCpuGraph);
        DeleteObject(hCpuGraphPen);

        // Erişilebilirlik: Renge bağımlı olmayan şekil işaretçileri (CPU ●, RAM ■)
        if (P.shapeMarkers) {
            HBRUSH hCpuMk = CreateSolidBrush(P.accentPrimary);
            HBRUSH hRamMk = CreateSolidBrush(P.accentSecondary);
            HPEN hMkPen = CreatePen(PS_SOLID, 1, P.cardBg);
            HPEN hOldMkPen = (HPEN)SelectObject(hdc, hMkPen);
            HBRUSH hOldMkBrush = (HBRUSH)SelectObject(hdc, hRamMk);
            for (size_t i = 0; i < historyCopy.size(); i += 6) {
                int ptX = gx + static_cast<int>((gw * i) / (MAX_HISTORY_POINTS - 1));

                double rVal = std::clamp(static_cast<double>(historyCopy[i].ramMB) / static_cast<double>(maxLimitMB), 0.0, 1.0);
                int rY = (gy + gh - 2) - static_cast<int>((gh - 4) * rVal);
                SelectObject(hdc, hRamMk);
                Rectangle(hdc, ptX - S_D(3), rY - S_D(3), ptX + S_D(4), rY + S_D(4));

                double cVal = std::clamp(historyCopy[i].cpuUsagePercent, 0.0, 100.0);
                int cY = (gy + gh - 2) - static_cast<int>((gh - 4) * (cVal / 100.0));
                SelectObject(hdc, hCpuMk);
                Ellipse(hdc, ptX - S_D(4), cY - S_D(4), ptX + S_D(5), cY + S_D(5));
            }
            SelectObject(hdc, hOldMkBrush);
            SelectObject(hdc, hOldMkPen);
            DeleteObject(hCpuMk);
            DeleteObject(hRamMk);
            DeleteObject(hMkPen);
        }
    }

    // Grafik Altındaki Lejant: ● CPU (%)    ● RAM (MB)
    SelectObject(hdc, hFontBold);
    int legY = gy + gh + S_D(22);
    int legCenterX = gx + gw / 2;

    // CPU Lejantı
    HBRUSH hCpuDotBrush = CreateSolidBrush(P.accentPrimary);
    HPEN hCpuDotPen = CreatePen(PS_SOLID, 1, P.accentPrimary);
    SelectObject(hdc, hCpuDotBrush);
    SelectObject(hdc, hCpuDotPen);
    Ellipse(hdc, legCenterX - S_D(78), legY + S_D(4), legCenterX - S_D(70), legY + S_D(12));
    DeleteObject(hCpuDotBrush);
    DeleteObject(hCpuDotPen);
    TextOutUtf8(hdc, legCenterX - S_D(66), legY, "CPU (%)", P.textPrimary);

    // RAM Lejantı
    HBRUSH hRamDotBrush = CreateSolidBrush(P.accentSecondary);
    HPEN hRamDotPen = CreatePen(PS_SOLID, 1, P.accentSecondary);
    SelectObject(hdc, hRamDotBrush);
    SelectObject(hdc, hRamDotPen);
    if (P.shapeMarkers) Rectangle(hdc, legCenterX + S_D(16), legY + S_D(4), legCenterX + S_D(24), legY + S_D(12));
    else Ellipse(hdc, legCenterX + S_D(16), legY + S_D(4), legCenterX + S_D(24), legY + S_D(12));
    DeleteObject(hRamDotBrush);
    DeleteObject(hRamDotPen);
    TextOutUtf8(hdc, legCenterX + S_D(29), legY, "RAM (MB)", P.textPrimary);

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

    if (msg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS | DLGC_WANTTAB | DLGC_WANTARROWS | DLGC_WANTCHARS;
    }

    if (msg == WM_CHAR) {
        if (wParam == VK_TAB) {
            return 0; // Tab karakteri basılmasını engelle
        }
    }

    if (msg == WM_KEYDOWN) {
        if (wParam != VK_TAB) {
            if (pThis) {
                pThis->resetTabCompletion();
            }
        }

        // TAB : Otomatik Tamamlama (Fikir 4)
        if (wParam == VK_TAB) {
            if (pThis) {
                pThis->handleTabCompletion();
            }
            return 0;
        }

        // F2 veya Ctrl+T : Erişilebilirlik temaları arasında geçiş
        if (wParam == VK_F2 || ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'T' || wParam == 't'))) {
            if (pThis) {
                pThis->cycleTheme();
            }
            return 0;
        }

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

            // Yerel "tema" komutu: ShellCore'a iletilmeden UIEngine içinde işlenir
            {
                std::string lowered = ToLowerAscii(cmd);
                size_t firstNonSpace = lowered.find_first_not_of(' ');
                std::string head = (firstNonSpace == std::string::npos) ? "" : lowered.substr(firstNonSpace);
                std::string word = head.substr(0, head.find(' '));
                if (word == "tema" || word == "theme") {
                    pThis->m_termHistory.push_back(cmd);
                    pThis->m_termHistoryIndex = (int)pThis->m_termHistory.size();
                    pThis->handleThemeCommand(head);
                    pThis->printPrompt();
                    return 0;
                }
            }

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

// ─────────────────────────────────────────────────────────────────────────────
// Dinamik Yeniden Boyutlandırma & En-Boy Oranı Koruma (Layout Scaling)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::updateLayout(int cx, int cy) {
    if (cx <= 0 || cy <= 0) return;

    const double TARGET_RATIO = 1400.0 / 800.0;
    int vw = cx;
    int vh = static_cast<int>(cx / TARGET_RATIO);
    if (vh > cy) {
        vh = cy;
        vw = static_cast<int>(cy * TARGET_RATIO);
    }

    m_viewportW = vw;
    m_viewportH = vh;
    m_viewportX = (cx - vw) / 2;
    m_viewportY = (cy - vh) / 2;
    m_scale = static_cast<double>(vw) / 1400.0;

    auto S_X = [this](int x) { return m_viewportX + static_cast<int>(x * m_scale); };
    auto S_Y = [this](int y) { return m_viewportY + static_cast<int>(y * m_scale); };
    auto S_W = [this](int w) { return (std::max)(10, static_cast<int>(w * m_scale)); };
    auto S_H = [this](int h) { return (std::max)(10, static_cast<int>(h * m_scale)); };

    if (m_hTerminalEdit && IsWindow(m_hTerminalEdit)) {
        MoveWindow(m_hTerminalEdit, S_X(28), S_Y(92), S_W(534), S_H(648), TRUE);
        int termFontSize = std::clamp(static_cast<int>(16 * m_scale), 10, 32);
        if (m_hTermFont) DeleteObject(m_hTermFont);
        m_hTermFont = CreateFontA(termFontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        if (m_hTermFont) {
            SendMessageA(m_hTerminalEdit, WM_SETFONT, (WPARAM)m_hTermFont, TRUE);
        }
    }

    if (m_hTrackRam && IsWindow(m_hTrackRam)) {
        MoveWindow(m_hTrackRam, S_X(610), S_Y(486), S_W(338), S_H(24), TRUE);
    }
    if (m_hTrackCpu && IsWindow(m_hTrackCpu)) {
        MoveWindow(m_hTrackCpu, S_X(610), S_Y(564), S_W(338), S_H(24), TRUE);
    }
    if (m_hBtnToggleSandbox && IsWindow(m_hBtnToggleSandbox)) {
        MoveWindow(m_hBtnToggleSandbox, S_X(616), S_Y(638), S_W(326), S_H(50), TRUE);
    }
    if (m_hBtnTheme && IsWindow(m_hBtnTheme)) {
        MoveWindow(m_hBtnTheme, S_X(996), S_Y(8), S_W(192), S_H(28), TRUE);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Terminal Otomatik Tamamlama (Tab Completion - Fikir 4)
// ─────────────────────────────────────────────────────────────────────────────
void UIEngine::handleTabCompletion() {
    if (!m_hTerminalEdit || !IsWindow(m_hTerminalEdit)) return;

    int textLen = GetWindowTextLengthA(m_hTerminalEdit);
    std::vector<char> buf(textLen + 1, 0);
    GetWindowTextA(m_hTerminalEdit, buf.data(), textLen + 1);

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

    // Tab ile ardışık eşleşmeler arasında döngü yapma
    if (!m_tabMatches.empty() && !m_lastTabPrefix.empty()) {
        m_tabMatchIndex = (m_tabMatchIndex + 1) % m_tabMatches.size();
        std::string chosen = m_tabMatches[m_tabMatchIndex];
        std::string newCmd = m_tabBase + chosen;
        if (m_tabMatches.size() == 1 && !chosen.empty() && chosen.back() != '\\') {
            newCmd += " ";
        }
        replaceCurrentInput(newCmd);
        return;
    }

    // Yeni tab tamamlama araması
    size_t lastSpace = cmd.find_last_of(" \t");
    std::string base;
    std::string word;
    if (lastSpace == std::string::npos) {
        base = "";
        word = cmd;
    } else {
        base = cmd.substr(0, lastSpace + 1);
        word = cmd.substr(lastSpace + 1);
    }

    std::vector<std::string> candidates;
    std::string baseTrim = base;
    while (!baseTrim.empty() && baseTrim.back() == ' ') baseTrim.pop_back();
    std::string baseLower = ToLowerAscii(baseTrim);

    if (baseLower.empty()) {
        // Yerleşik komut sözlüğü
        candidates = {
            "bench", "cat", "cd", "chdir", "clear", "cls", "copy", "cp",
            "date", "del", "delete", "demo", "dir", "echo", "erase", "exit",
            "export", "help", "history", "job", "kill", "ls", "md", "mkdir",
            "move", "mv", "procs", "ps", "quit", "rd", "ren", "rename",
            "report", "rm", "rmdir", "sandbox", "stress", "sysinfo",
            "systeminfo", "taskkill", "tema", "theme", "time", "timeit",
            "touch", "type"
        };
    } else if (baseLower == "sandbox" || baseLower == "job") {
        candidates = { "on", "off", "status", "set", "ram", "cpu" };
    } else if (baseLower == "sandbox set") {
        candidates = { "ram", "cpu" };
    } else if (baseLower == "tema" || baseLower == "theme") {
        candidates = {
            "normal", "protan", "deutan", "tritan", "sonraki",
            "goster", "renk", "isaret", "sifirla", "kaydet", "yukle", "yardim"
        };
    } else if (baseLower == "ps" || baseLower == "procs") {
        candidates = { "top", "mem", "cpu" };
    }

    // Bulunulan dizindeki dosya ve klasörleri de eşleştir
    WIN32_FIND_DATAA fd;
    std::string searchPat = word + "*";
    HANDLE hFind = FindFirstFileA(searchPat.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string fname = fd.cFileName;
            if (fname != "." && fname != "..") {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    candidates.push_back(fname + "\\");
                } else {
                    candidates.push_back(fname);
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    // Prefixe göre filtrele (büyük/küçük harf duyarsız)
    std::vector<std::string> matches;
    for (const auto& c : candidates) {
        if (c.size() >= word.size()) {
            if (_strnicmp(c.c_str(), word.c_str(), word.size()) == 0) {
                if (std::find(matches.begin(), matches.end(), c) == matches.end()) {
                    matches.push_back(c);
                }
            }
        }
    }

    if (matches.empty()) return;

    m_tabMatches = matches;
    m_tabMatchIndex = 0;
    m_lastTabPrefix = word;
    m_tabBase = base;

    std::string chosen = m_tabMatches[0];
    std::string newCmd = base + chosen;
    if (matches.size() == 1 && !chosen.empty() && chosen.back() != '\\') {
        newCmd += " ";
    }
    replaceCurrentInput(newCmd);
}

void UIEngine::resetTabCompletion() {
    m_tabMatches.clear();
    m_tabMatchIndex = 0;
    m_lastTabPrefix.clear();
    m_tabBase.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Erişilebilirlik Tema Yönetimi (Renk Körlüğü Temaları)
// ─────────────────────────────────────────────────────────────────────────────
ThemePalette UIEngine::activePalette() const {
    std::lock_guard<std::mutex> lock(m_themeMutex);
    return m_palettes[ThemeIndex(static_cast<ColorBlindTheme>(m_activeTheme.load()))];
}

void UIEngine::applyThemeChange() {
    // GDI fırçalarının yeniden oluşturulması GUI thread'ine devredilir
    if (m_hWnd && IsWindow(m_hWnd)) {
        PostMessage(m_hWnd, WM_APP_THEME_CHANGED, 0, 0);
    }
}

void UIEngine::setTheme(ColorBlindTheme theme) {
    m_activeTheme.store(ThemeIndex(theme));
    applyThemeChange();
}

ColorBlindTheme UIEngine::getTheme() const {
    return static_cast<ColorBlindTheme>(ThemeIndex(static_cast<ColorBlindTheme>(m_activeTheme.load())));
}

void UIEngine::cycleTheme() {
    int next = (m_activeTheme.load() + 1) % static_cast<int>(ColorBlindTheme::Count);
    setTheme(static_cast<ColorBlindTheme>(next));
}

void UIEngine::setThemePalette(ColorBlindTheme theme, const ThemePalette& palette) {
    {
        std::lock_guard<std::mutex> lock(m_themeMutex);
        m_palettes[ThemeIndex(theme)] = palette;
    }
    if (ThemeIndex(theme) == m_activeTheme.load()) applyThemeChange();
}

ThemePalette UIEngine::getThemePalette(ColorBlindTheme theme) const {
    std::lock_guard<std::mutex> lock(m_themeMutex);
    return m_palettes[ThemeIndex(theme)];
}

bool UIEngine::setThemeColor(ColorBlindTheme theme, const std::string& key, COLORREF color) {
    std::string k = ToLowerAscii(key);
    for (const auto& pk : kPaletteKeys) {
        if (ToLowerAscii(pk.key) == k) {
            {
                std::lock_guard<std::mutex> lock(m_themeMutex);
                m_palettes[ThemeIndex(theme)].*(pk.member) = color;
            }
            if (ThemeIndex(theme) == m_activeTheme.load()) applyThemeChange();
            return true;
        }
    }
    return false;
}

void UIEngine::resetThemePalette(ColorBlindTheme theme) {
    setThemePalette(theme, defaultPalette(theme));
}

std::string UIEngine::themeFilePath(const std::string& path) const {
    if (!path.empty()) return path;
    char exePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir = (len > 0) ? std::string(exePath, len) : std::string();
    size_t slash = dir.find_last_of("\\/");
    dir = (slash != std::string::npos) ? dir.substr(0, slash) : ".";
    return dir + "\\specter_themes.ini";
}

bool UIEngine::loadThemesFromFile(const std::string& path) {
    const std::string file = themeFilePath(path);
    if (GetFileAttributesA(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    char buf[64];
    {
        std::lock_guard<std::mutex> lock(m_themeMutex);
        for (int t = 0; t < static_cast<int>(ColorBlindTheme::Count); ++t) {
            const char* section = kThemeIniSections[t];
            for (const auto& pk : kPaletteKeys) {
                GetPrivateProfileStringA(section, pk.key, "", buf, sizeof(buf), file.c_str());
                COLORREF c;
                if (ParseColor(buf, c)) {
                    m_palettes[t].*(pk.member) = c;
                }
            }
            GetPrivateProfileStringA(section, "shapeMarkers", "", buf, sizeof(buf), file.c_str());
            if (buf[0] != '\0') {
                m_palettes[t].shapeMarkers = (buf[0] == '1' || buf[0] == 't' || buf[0] == 'T');
            }
        }
    }

    GetPrivateProfileStringA("General", "activeTheme", "", buf, sizeof(buf), file.c_str());
    ColorBlindTheme active = ColorBlindTheme::Normal;
    if (buf[0] != '\0' && ParseThemeName(buf, active)) {
        m_activeTheme.store(ThemeIndex(active));
    }

    applyThemeChange();
    return true;
}

bool UIEngine::saveThemesToFile(const std::string& path) const {
    const std::string file = themeFilePath(path);
    bool ok = WritePrivateProfileStringA("General", "activeTheme",
        kThemeIniSections[ThemeIndex(getTheme())], file.c_str()) != FALSE;

    std::lock_guard<std::mutex> lock(m_themeMutex);
    for (int t = 0; t < static_cast<int>(ColorBlindTheme::Count) && ok; ++t) {
        const char* section = kThemeIniSections[t];
        for (const auto& pk : kPaletteKeys) {
            ok = ok && WritePrivateProfileStringA(section, pk.key,
                ColorToHex(m_palettes[t].*(pk.member)).c_str(), file.c_str()) != FALSE;
        }
        ok = ok && WritePrivateProfileStringA(section, "shapeMarkers",
            m_palettes[t].shapeMarkers ? "1" : "0", file.c_str()) != FALSE;
    }
    return ok;
}

// Terminal komutu: tema [liste|<ad>|sonraki|goster|renk|isaret|sifirla|kaydet|yukle|yardim]
void UIEngine::handleThemeCommand(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::vector<std::string> args;
    std::string tok;
    while (iss >> tok) args.push_back(tok);

    auto out = [this](const std::string& s) { printTerminalText(s + "\r\n"); };
    const ColorBlindTheme current = getTheme();

    auto printHelp = [&]() {
        out("Erişilebilirlik Temaları - Kullanım:");
        out("  tema                          : Temaları listele");
        out("  tema <normal|protan|deutan|tritan> : Temayı uygula");
        out("  tema sonraki                  : Sıradaki tema (Kısayol: F2 / Ctrl+T)");
        out("  tema goster [tema]            : Palet renklerini göster");
        out("  tema renk <anahtar> <#RRGGBB> [tema] : Rengi özelleştir");
        out("  tema isaret <ac|kapat> [tema] : Grafik şekil işaretçileri");
        out("  tema sifirla [tema|hepsi]     : Fabrika ayarlarına dön");
        out("  tema kaydet / tema yukle      : specter_themes.ini kaydet/yükle");
    };

    auto printList = [&]() {
        static const char* descs[] = {
            "Varsayılan neon palet",
            "Kırmızı körlüğü  (Mavi / Sarı ekseni)",
            "Yeşil körlüğü    (Mavi / Turuncu ekseni)",
            "Mavi körlüğü     (Kırmızı / Camgöbeği ekseni)"
        };
        static const char* ids[] = { "normal", "protan", "deutan", "tritan" };
        out("Erişilebilirlik Temaları:");
        for (int i = 0; i < static_cast<int>(ColorBlindTheme::Count); ++i) {
            std::ostringstream line;
            line << (i == ThemeIndex(current) ? "  * " : "    ")
                 << std::left << std::setw(8) << ids[i] << " " << descs[i];
            out(line.str());
        }
        out("Yardım için: tema yardim");
    };

    // İsteğe bağlı tema argümanını çözer; yoksa aktif tema kullanılır
    auto themeArg = [&](size_t idx, ColorBlindTheme& t) -> bool {
        if (idx >= args.size()) { t = current; return true; }
        if (ParseThemeName(args[idx], t)) return true;
        out("Hata: Bilinmeyen tema '" + args[idx] + "'. (normal|protan|deutan|tritan)");
        return false;
    };

    if (args.size() < 2 || args[1] == "liste" || args[1] == "list") {
        printList();
        return;
    }

    const std::string& sub = args[1];
    ColorBlindTheme target;

    if (sub == "yardim" || sub == "help" || sub == "?") {
        printHelp();
    }
    else if (sub == "sonraki" || sub == "next") {
        cycleTheme();
        out(std::string("Tema uygulandı: ") + themeDisplayName(getTheme()));
    }
    else if (sub == "goster" || sub == "show") {
        if (!themeArg(2, target)) return;
        ThemePalette p = getThemePalette(target);
        out(std::string("Palet: ") + themeDisplayName(target));
        for (const auto& pk : kPaletteKeys) {
            std::ostringstream line;
            line << "  " << std::left << std::setw(16) << pk.key << " " << ColorToHex(p.*(pk.member)) << "  " << pk.desc;
            out(line.str());
        }
        out(std::string("  shapeMarkers     ") + (p.shapeMarkers ? "acik" : "kapali") + "    Grafik şekil işaretçileri");
    }
    else if (sub == "renk" || sub == "color") {
        if (args.size() < 4) {
            out("Kullanım: tema renk <anahtar> <#RRGGBB | R,G,B> [tema]");
            return;
        }
        COLORREF c;
        if (!ParseColor(args[3], c)) {
            out("Hata: Geçersiz renk '" + args[3] + "'. Örnek: #FFB000 veya 255,176,0");
            return;
        }
        if (!themeArg(4, target)) return;
        if (setThemeColor(target, args[2], c)) {
            out("Renk güncellendi: " + args[2] + " = " + ColorToHex(c) + " (" + themeDisplayName(target) + ")");
        } else {
            out("Hata: Bilinmeyen anahtar '" + args[2] + "'. Anahtarlar için: tema goster");
        }
    }
    else if (sub == "isaret" || sub == "markers") {
        if (args.size() < 3 || (args[2] != "ac" && args[2] != "kapat" && args[2] != "on" && args[2] != "off")) {
            out("Kullanım: tema isaret <ac|kapat> [tema]");
            return;
        }
        if (!themeArg(3, target)) return;
        ThemePalette p = getThemePalette(target);
        p.shapeMarkers = (args[2] == "ac" || args[2] == "on");
        setThemePalette(target, p);
        out(std::string("Şekil işaretçileri ") + (p.shapeMarkers ? "açıldı" : "kapatıldı") + " (" + themeDisplayName(target) + ")");
    }
    else if (sub == "sifirla" || sub == "reset") {
        if (args.size() >= 3 && (args[2] == "hepsi" || args[2] == "all")) {
            for (int i = 0; i < static_cast<int>(ColorBlindTheme::Count); ++i) {
                resetThemePalette(static_cast<ColorBlindTheme>(i));
            }
            out("Tüm temalar fabrika ayarlarına döndürüldü.");
            return;
        }
        if (!themeArg(2, target)) return;
        resetThemePalette(target);
        out(std::string("Tema fabrika ayarlarına döndürüldü: ") + themeDisplayName(target));
    }
    else if (sub == "kaydet" || sub == "save") {
        if (saveThemesToFile()) out("Temalar kaydedildi: " + themeFilePath(""));
        else out("Hata: Tema dosyası yazılamadı: " + themeFilePath(""));
    }
    else if (sub == "yukle" || sub == "load") {
        if (loadThemesFromFile()) out(std::string("Temalar yüklendi. Aktif tema: ") + themeDisplayName(getTheme()));
        else out("Hata: Tema dosyası bulunamadı: " + themeFilePath(""));
    }
    else if (ParseThemeName(sub, target)) {
        setTheme(target);
        out(std::string("Tema uygulandı: ") + themeDisplayName(target));
    }
    else {
        out("Hata: Bilinmeyen alt komut '" + sub + "'.");
        printHelp();
    }
}

} // namespace SpecTer
