#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include "Profiler.h"
#include "JobManager.h"

namespace SpecTer {

using OSVisualizer::ProcessMetrics;
using OSVisualizer::SandboxConfig;

/**
 * @struct DashboardState
 * @brief 4. Üye: Canlı GUI paneli için gerekli tüm metrik ve durum verilerini tutan yapı.
 */
struct DashboardState {
    ProcessMetrics metrics;            ///< 3. Üye'den gelen canlı RAM, CPU, Handle ve Thread verileri
    SandboxConfig sandboxConfig;       ///< 2. Üye'den gelen RAM/CPU limit ayarları
    bool sandboxEnabled = false;       ///< Sandbox modu açık mı?
    DWORD activePID = 0;               ///< Çalışmakta olan aktif sürecin PID'si
    std::string processName;           ///< Çalıştırılan uygulamanın adı (örn. ping.exe, cmd.exe)
};

/**
 * @struct MetricSample
 * @brief Zaman serisi grafiği için anlık örnek veri yapısı.
 */
struct MetricSample {
    double cpuUsagePercent = 0.0;
    size_t ramMB = 0;
};

using SandboxUpdateCallback = std::function<void(const SandboxConfig&, bool)>;

/**
 * @class UIEngine
 * @brief 4. Üye Sorumluluğu: Profesyonel Grafiksel Arayüz (GUI Visual Dashboard).
 * 
 * 4. ÜYE İÇİN UYGULAMA REHBERİ:
 * -----------------------------------------------------------------------------
 * Bu sınıf, basit konsol yazısı yerine grafiksel, yüksek kaliteli canlı bir
 * izleme ve denetim paneli (Dashboard GUI) sunmaktan sorumludur.
 * 
 * Geliştirilecek Görsel Bileşenler:
 *  1. [RAM & CPU Gauges] : Anlık bellek ve CPU kullanımını gösteren dairesel/çubuk grafikler.
 *  2. [Handle Tree View] : İşletim sistemi Kernel Handle'larını (File, Pipe, Thread) hiyerarşik gösterim.
 *  3. [Sandbox Panel]   : RAM/CPU limitlerini ayarlamak için interaktif sürgüler/butonlar.
 *  4. [Real-time Graph] : Zaman serisi grafik çizimi (Direct2D / GDI+ / Win32 GUI / Custom Window).
 */
class UIEngine {
public:
    UIEngine();
    ~UIEngine();

    /**
     * @brief Grafiksel GUI penceresini oluşturur ve başlatır.
     * @param hInstance Win32 uygulama örneği handle'ı.
     * @return Başarılı ise true.
     */
    bool initDashboardWindow(HINSTANCE hInstance = NULL);

    /**
     * @brief Canlı metrik ve sandbox durum verilerini GUI arayüzüne gönderir ve paneli günceller.
     * @param state Anlık metrikler ve sandbox durumunu içeren yapı.
     */
    void updateDashboard(const DashboardState& state);

    /**
     * @brief GUI penceresinin açık ve çalışır durumda olup olmadığını sorgular.
     */
    bool isWindowOpen() const;

    /**
     * @brief GUI penceresini kapatır ve kaynakları serbest bırakır.
     */
    void closeDashboard();

    /**
     * @brief Sandbox ayarları değiştiğinde çağrılacak callback fonksiyonunu ayarlar.
     */
    void setSandboxUpdateCallback(SandboxUpdateCallback callback);

    /**
     * @brief Aktif çalışmakta olan alt süreci canlı profilleme için bildirim metodu.
     */
    void setActiveProcess(HANDLE hProcess, DWORD pid, const std::string& name);

private:
    void guiThreadFunc(HINSTANCE hInstance);
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void setupControls(HWND hWnd);
    void handleHScroll(HWND hWnd, WPARAM wParam, LPARAM lParam);
    void handleCommand(HWND hWnd, WPARAM wParam, LPARAM lParam);
    void onPaint(HWND hWnd);

private:
    HWND m_hWnd = NULL;                          ///< Win32 GUI Pencere Handle'ı
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_windowReady{false};
    std::thread m_guiThread;
    std::mutex m_stateMutex;

    DashboardState m_state;
    static constexpr size_t MAX_HISTORY_POINTS = 60;
    std::deque<MetricSample> m_history;

    SandboxUpdateCallback m_sandboxCallback = nullptr;

    // Win32 Kontrol Handle'ları
    HWND m_hTrackRam = NULL;
    HWND m_hTrackCpu = NULL;
    HWND m_hBtnToggleSandbox = NULL;
    HWND m_hBtnApply = NULL;
    HWND m_hLblRamVal = NULL;
    HWND m_hLblCpuVal = NULL;

    // GDI Kaynakları
    HBRUSH m_hStaticBrush = NULL;               ///< WM_CTLCOLORSTATIC için kart arkaplan fırçası

    // Canlı profilleme için aktif süreç ve Profiler
    HANDLE m_hActiveProcess = NULL;
    OSVisualizer::Profiler m_internalProfiler;
};

} // namespace SpecTer

#endif // UI_ENGINE_H
