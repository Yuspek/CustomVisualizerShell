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
    Profiler() = default;
    ~Profiler() = default;

    /**
     * @brief Sürecin anlık RAM ve CPU kullanımını hesaplar.
     */
    ProcessMetrics sampleProcess(HANDLE hProcess) {
        ProcessMetrics metrics;
        if (hProcess == NULL || hProcess == INVALID_HANDLE_VALUE) return metrics;

        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
            metrics.workingSetSizeMB = pmc.WorkingSetSize / (1024 * 1024);
            metrics.peakWorkingSetSizeMB = pmc.PeakWorkingSetSize / (1024 * 1024);
        }

        // 3. Üye buraya CPU zamanı hesaplama ve Toolhelp32 tarama kodlarını ekleyecektir.
        return metrics;
    }
};

} // namespace OSVisualizer

#endif // PROFILER_H
