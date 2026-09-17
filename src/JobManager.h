#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <windows.h>
#include "ShellCore.h"

namespace OSVisualizer {

/**
 * @class JobManager
 * @brief 2. Üye Sorumluluğu: Windows Job Objects API ile süreç kısıtlama ve güvenlik sandboxing.
 * 
 * Gelecekte Kullanılacak Win32 API'leri:
 * - CreateJobObject
 * - SetInformationJobObject (JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JOBOBJECT_CPU_RATE_CONTROL_INFORMATION)
 * - AssignProcessToJobObject
 */
class JobManager {
public:
    JobManager() = default;
    ~JobManager() = default;

    /**
     * @brief Süreci Job Object'e bağlar ve belirlenen RAM/CPU sınırlarını uygular.
     */
    bool applySandboxLimits(const ProcessInfo& procInfo, size_t maxMemoryBytes, DWORD maxCpuPercent) {
        // 2. Üye buraya Job Objects kodlarını entegre edecektir.
        (void)procInfo;
        (void)maxMemoryBytes;
        (void)maxCpuPercent;
        return true;
    }
};

} // namespace OSVisualizer

#endif // JOB_MANAGER_H
