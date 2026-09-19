#ifndef PROFILER_H
#define PROFILER_H

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include "ShellCore.h"

namespace OSVisualizer {

/**
 * @struct ProcessMetrics
 * @brief 3. Üye Sorumluluğu: Anlık CPU, RAM ve Handle sayımlarını tutan metrik yapısı.
 */
struct ProcessMetrics {
    size_t workingSetSizeMB = 0;
    size_t peakWorkingSetSizeMB = 0;
    double cpuUsagePercent = 0.0;
    DWORD openHandleCount = 0;
    DWORD threadCount = 0;
};

/**
 * @class Profiler
 * @brief 3. Üye Sorumluluğu: GetProcessMemoryInfo, GetProcessTimes ve Toolhelp32 API ile izleme motoru.
 */
class Profiler {
public:
    Profiler()
        : m_prevKernelTime(0)
        , m_prevUserTime(0)
        , m_prevWallTime(0) {}
    ~Profiler() = default;

    /**
     * @brief Sürecin anlık RAM ve CPU kullanımını hesaplar.
     *
     * Win32 API Çağrıları:
     *   - GetProcessMemoryInfo : RAM (WorkingSet) ölçümü
     *   - GetProcessTimes      : CPU zamanı hesaplaması
     *   - CreateToolhelp32Snapshot + Thread32First/Next : Thread sayımı
     *   - GetProcessHandleCount: Açık handle sayısı
     */
    ProcessMetrics sampleProcess(HANDLE hProcess);

private:
    /// @brief CPU delta hesabı için önceki ölçüm değerleri (100-nanosecond FILETIME birimleri)
    ULONGLONG m_prevKernelTime;  ///< Önceki Kernel zamanı
    ULONGLONG m_prevUserTime;    ///< Önceki User zamanı
    ULONGLONG m_prevWallTime;    ///< Önceki duvar saati (wall clock) zamanı
};

} // namespace OSVisualizer

#endif // PROFILER_H
