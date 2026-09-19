#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <windows.h>
#include <string>
#include <vector>
#include "Profiler.h"
#include "JobManager.h"

namespace SpecTer {

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
    UIEngine() = default;
    ~UIEngine() = default;

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

private:
    HWND m_hWnd = NULL;  ///< Win32 GUI Pencere Handle'ı
};

} // namespace SpecTer

#endif // UI_ENGINE_H
