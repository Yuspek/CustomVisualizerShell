#include "Profiler.h"

namespace OSVisualizer {

/**
 * @brief Sürecin anlık RAM, CPU, thread ve handle metriklerini toplar.
 *
 * Algoritma Özeti:
 *   1. GetProcessMemoryInfo  → WorkingSetSize ve PeakWorkingSetSize (MB).
 *   2. GetProcessTimes       → KernelTime + UserTime delta'larını sistem saati delta'sına
 *                               oranlayarak anlık CPU yüzdesi hesaplar.
 *   3. GetProcessHandleCount → Sürecin açık kernel handle adedini sorgular.
 *   4. CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD) + Thread32First/Next
 *                             → Sistemdeki tüm thread'leri tarayıp yalnızca hedef PID'ye
 *                               ait olanları sayar.
 *
 * Not: CPU hesabı iki ardışık çağrı arasındaki farkı kullanır (delta-based).
 *      İlk çağrıda önceki değer olmadığından cpuUsagePercent = 0.0 döner.
 */
ProcessMetrics Profiler::sampleProcess(HANDLE hProcess) {
    ProcessMetrics metrics;

    // ── Guard: Geçersiz handle kontrolü ──
    if (hProcess == NULL || hProcess == INVALID_HANDLE_VALUE) {
        return metrics;
    }

    // ══════════════════════════════════════════════════════════════════
    //  1) RAM Ölçümü — GetProcessMemoryInfo (psapi.h)
    // ══════════════════════════════════════════════════════════════════
    PROCESS_MEMORY_COUNTERS pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);

    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        metrics.workingSetSizeMB     = pmc.WorkingSetSize     / (1024 * 1024);
        metrics.peakWorkingSetSizeMB = pmc.PeakWorkingSetSize / (1024 * 1024);
    }

    // ══════════════════════════════════════════════════════════════════
    //  2) CPU Kullanım Yüzdesi — GetProcessTimes (windows.h)
    //
    //  FILETIME 100-nanosecond birimlerindedir.
    //  CPU% = (deltaKernel + deltaUser) / deltaWall * 100
    //
    //  İlk çağrıda prev değerleri sıfır olduğu için delta hesabı yapılamaz,
    //  cpuUsagePercent = 0.0 olarak kalır.
    // ══════════════════════════════════════════════════════════════════
    FILETIME ftCreation, ftExit, ftKernel, ftUser;

    if (GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
        // FILETIME → 64-bit tamsayıya dönüştürme yardımcısı
        auto toUInt64 = [](const FILETIME& ft) -> ULONGLONG {
            ULARGE_INTEGER ul;
            ul.LowPart  = ft.dwLowDateTime;
            ul.HighPart = ft.dwHighDateTime;
            return ul.QuadPart;
        };

        ULONGLONG currentKernel = toUInt64(ftKernel);
        ULONGLONG currentUser   = toUInt64(ftUser);

        // Sistem duvar saati (wall clock) ölçümü
        FILETIME ftNow;
        GetSystemTimeAsFileTime(&ftNow);
        ULONGLONG currentWall = toUInt64(ftNow);

        // Delta hesabı — önceki ölçüm varsa
        if (m_prevWallTime > 0) {
            ULONGLONG deltaKernel = currentKernel - m_prevKernelTime;
            ULONGLONG deltaUser   = currentUser   - m_prevUserTime;
            ULONGLONG deltaWall   = currentWall   - m_prevWallTime;

            if (deltaWall > 0) {
                metrics.cpuUsagePercent =
                    static_cast<double>(deltaKernel + deltaUser) /
                    static_cast<double>(deltaWall) * 100.0;

                // Clamp: Tek çekirdek bazında %100'ü geçebilir ama negatif olamaz
                if (metrics.cpuUsagePercent < 0.0) {
                    metrics.cpuUsagePercent = 0.0;
                }
            }
        }

        // Mevcut değerleri bir sonraki çağrı için sakla
        m_prevKernelTime = currentKernel;
        m_prevUserTime   = currentUser;
        m_prevWallTime   = currentWall;
    }

    // ══════════════════════════════════════════════════════════════════
    //  3) Açık Handle Sayısı — GetProcessHandleCount (windows.h)
    // ══════════════════════════════════════════════════════════════════
    DWORD handleCount = 0;
    if (GetProcessHandleCount(hProcess, &handleCount)) {
        metrics.openHandleCount = handleCount;
    }

    // ══════════════════════════════════════════════════════════════════
    //  4) Thread Sayımı — Toolhelp32 API (tlhelp32.h)
    //
    //  CreateToolhelp32Snapshot ile sistemdeki tüm thread'lerin anlık
    //  görüntüsünü alıp, hedef sürecin PID'sine sahip olanları sayar.
    // ══════════════════════════════════════════════════════════════════
    DWORD targetPID = GetProcessId(hProcess);

    if (targetPID != 0) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

        if (hSnapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te32;
            ZeroMemory(&te32, sizeof(te32));
            te32.dwSize = sizeof(THREADENTRY32);

            DWORD threadCount = 0;

            if (Thread32First(hSnapshot, &te32)) {
                do {
                    if (te32.th32OwnerProcessID == targetPID) {
                        ++threadCount;
                    }
                } while (Thread32Next(hSnapshot, &te32));
            }

            metrics.threadCount = threadCount;
            CloseHandle(hSnapshot);
        }
    }

    return metrics;
}

} // namespace OSVisualizer
