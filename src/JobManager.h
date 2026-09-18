#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include "ShellCore.h"

namespace OSVisualizer {

/**
 * @struct SandboxConfig
 * @brief 2. Üye: Sandbox kısıtlama parametrelerini tutan yapılandırma yapısı.
 *
 * maxMemoryMB  : Sürecin kullanabileceği maksimum RAM miktarı (MB cinsinden).
 * maxCpuPercent: Sürecin kullanabileceği maksimum CPU yüzdesi (1-100 arası).
 */
struct SandboxConfig {
    size_t maxMemoryMB   = 256;   ///< Varsayılan RAM limiti: 256 MB
    DWORD  maxCpuPercent = 50;    ///< Varsayılan CPU limiti: %50
};

/**
 * @struct SandboxResult
 * @brief 2. Üye: Job Object oluşturma ve atama işleminin sonucunu raporlayan yapı.
 */
struct SandboxResult {
    bool        success = false;       ///< İşlem başarılı mı?
    std::string errorMessage;          ///< Hata durumunda açıklama mesajı
    HANDLE      hJob    = NULL;        ///< Oluşturulan Job Object handle'ı
};

/**
 * @class JobManager
 * @brief 2. Üye Sorumluluğu: Windows Job Objects API ile süreç kısıtlama ve güvenlik sandboxing.
 *
 * Kullanılan Win32 API'leri:
 * - CreateJobObjectA         : Anonim Job Object oluşturma
 * - SetInformationJobObject  : RAM limiti (JOBOBJECT_EXTENDED_LIMIT_INFORMATION)
 *                              CPU limiti (JOBOBJECT_CPU_RATE_CONTROL_INFORMATION)
 * - AssignProcessToJobObject : Süreci Job Object'e bağlama
 * - QueryInformationJobObject: Job'dan mevcut limit bilgilerini sorgulama
 *
 * Akış:
 *   ShellCore süreç başlatır (CREATE_SUSPENDED) → Hook tetiklenir →
 *   JobManager Job oluşturur, limitleri ayarlar, süreci atar →
 *   ShellCore ResumeThread ile süreci devam ettirir.
 */
class JobManager {
public:
    JobManager();
    ~JobManager();

    // Kopyalama ve taşıma engellendi (Job handle'ı benzersizdir)
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    /**
     * @brief Yeni bir Job Object oluşturur, RAM ve CPU sınırlarını uygular
     *        ve süreci bu Job'a atar.
     * @param procInfo ShellCore tarafından doldurulan süreç bilgileri (hProcess gerekli).
     * @return SandboxResult — başarı durumu, hata mesajı ve Job handle'ı.
     *
     * Win32 API Çağrı Sırası:
     *   1. CreateJobObjectA(NULL, NULL)
     *   2. SetInformationJobObject(..., JobObjectExtendedLimitInformation, ...)
     *   3. SetInformationJobObject(..., JobObjectCpuRateControlInformation, ...)
     *   4. AssignProcessToJobObject(hJob, hProcess)
     */
    SandboxResult createJobAndApply(ProcessInfo& procInfo);

    /**
     * @brief Sandbox konfigürasyonunu günceller (RAM ve CPU limitleri).
     * @param cfg Yeni SandboxConfig yapısı.
     */
    void setConfig(const SandboxConfig& cfg);

    /**
     * @brief Mevcut sandbox konfigürasyonunu döndürür.
     */
    SandboxConfig getConfig() const;

    /**
     * @brief Sandbox modunu açar veya kapatır.
     * @param enabled true: Sandbox aktif, false: Sandbox devre dışı.
     */
    void setEnabled(bool enabled);

    /**
     * @brief Sandbox modunun aktif olup olmadığını sorgular.
     */
    bool isEnabled() const;

    /**
     * @brief Mevcut sandbox durumunu ve ayarlarını okunabilir metin olarak döndürür.
     * @return Terminale yazdırılabilecek durum raporu stringi.
     */
    std::string getStatusReport() const;

    /**
     * @brief Shell'den gelen sandbox komutlarını işler.
     * @param args Ayrıştırılmış komut argümanları (args[0] = "sandbox" veya "job").
     * @return Komut işlendiyse true.
     *
     * Desteklenen komutlar:
     *   sandbox on            — Sandbox modunu aktifleştirir
     *   sandbox off           — Sandbox modunu devre dışı bırakır
     *   sandbox set ram <MB>  — RAM limitini ayarlar
     *   sandbox set cpu <%>   — CPU limitini ayarlar
     *   sandbox status        — Mevcut durumu ve ayarları gösterir
     */
    bool handleCommand(const std::vector<std::string>& args);

private:
    bool          m_enabled;  ///< Sandbox modu açık/kapalı durumu
    SandboxConfig m_config;   ///< Aktif sandbox konfigürasyonu

    /**
     * @brief Win32 GetLastError() kodunu okunabilir hata mesajına çevirir.
     * @param dwError Hata kodu.
     * @return İnsan tarafından okunabilir hata mesajı stringi.
     */
    static std::string formatWin32Error(DWORD dwError);
};

} // namespace OSVisualizer

#endif // JOB_MANAGER_H
