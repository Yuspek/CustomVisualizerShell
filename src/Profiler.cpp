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

    // ── Guard: Geçersiz handle kontrolü (Win32'de GetCurrentProcess() == (HANDLE)-1 olduğundan özel ele alınır) ──
    if (hProcess == NULL) {
        return metrics;
    }

    DWORD targetPID = 0;
    HANDLE hEffective = hProcess;
    bool bNeedClose = false;

    if (hProcess == (HANDLE)-1 || hProcess == GetCurrentProcess()) {
        targetPID = GetCurrentProcessId();
        HANDLE hOpened = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, targetPID);
        if (hOpened != NULL && hOpened != INVALID_HANDLE_VALUE) {
            hEffective = hOpened;
            bNeedClose = true;
        }
    } else {
        targetPID = GetProcessId(hProcess);
    }

    if (targetPID == 0) {
        if (hProcess == INVALID_HANDLE_VALUE) {
            return metrics;
        }
    }


    // Farklı bir sürece geçildiyse CPU delta tabanını sıfırla (yanlış/taşmış değer üretmemek için)
    if (targetPID != m_prevPid) {
        reset();
        m_prevPid = targetPID;
    }

    // ══════════════════════════════════════════════════════════════════
    //  1) RAM Ölçümü — GetProcessMemoryInfo (psapi.h)
    // ══════════════════════════════════════════════════════════════════
    PROCESS_MEMORY_COUNTERS pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);

    if (GetProcessMemoryInfo(hEffective, &pmc, sizeof(pmc))) {
        metrics.workingSetSizeMB     = (pmc.WorkingSetSize > 0) ? std::max<size_t>(1, pmc.WorkingSetSize / (1024 * 1024)) : 0;
        metrics.peakWorkingSetSizeMB = (pmc.PeakWorkingSetSize > 0) ? std::max<size_t>(1, pmc.PeakWorkingSetSize / (1024 * 1024)) : 0;
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

        // Delta hesabı — önceki ölçüm varsa (ve sayaçlar geriye gitmediyse)
        if (m_prevWallTime > 0 && currentWall > m_prevWallTime &&
            currentKernel >= m_prevKernelTime && currentUser >= m_prevUserTime) {
            ULONGLONG deltaKernel = currentKernel - m_prevKernelTime;
            ULONGLONG deltaUser   = currentUser   - m_prevUserTime;
            ULONGLONG deltaWall   = currentWall   - m_prevWallTime;

            metrics.cpuUsagePercent =
                static_cast<double>(deltaKernel + deltaUser) /
                static_cast<double>(deltaWall) * 100.0;
            // Not: Tek çekirdek bazında %100'ü geçebilir (çok çekirdekli kullanım).
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
    if (GetProcessHandleCount(hEffective, &handleCount)) {
        metrics.openHandleCount = handleCount;
    }

    if (bNeedClose && hEffective != NULL && hEffective != INVALID_HANDLE_VALUE) {
        CloseHandle(hEffective);
        hEffective = hProcess;
    }


    // ══════════════════════════════════════════════════════════════════
    //  4) Thread Sayımı — Toolhelp32 API (tlhelp32.h)
    //
    //  CreateToolhelp32Snapshot ile sistemdeki tüm thread'lerin anlık
    //  görüntüsünü alıp, hedef sürecin PID'sine sahip olanları sayar.
    // ══════════════════════════════════════════════════════════════════
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
