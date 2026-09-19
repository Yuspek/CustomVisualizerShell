#include "ShellCore.h"
#include "JobManager.h"
#include "Profiler.h"
#include <iostream>
#include <iomanip>

int main() {
    try {
        ShellCore shell;
        OSVisualizer::JobManager jobManager;
        OSVisualizer::Profiler profiler;

        // 2. Üye & 3. Üye Entegrasyonu: Süreç başlatıldığında Sandboxing & Profiling
        shell.setProcessCreatedHook([&jobManager, &profiler](ProcessInfo& procInfo) {
            // 2. Üye: JobManager Sandbox Kısıtlamaları
            if (jobManager.isEnabled()) {
                OSVisualizer::SandboxResult result = jobManager.createJobAndApply(procInfo);
                if (!result.success) {
                    std::cerr << "[JobManager HATA]: " << result.errorMessage << "\n";
                }
            }

            // 3. Üye: Profiler Canlı Süreç Metrikleri
            if (procInfo.hProcess != NULL) {
                OSVisualizer::ProcessMetrics metrics = profiler.sampleProcess(procInfo.hProcess);
                std::cout << "  +--------------------------------------------------+\n";
                std::cout << "  |          PROFILER METRİKLERİ (3. Üye)            |\n";
                std::cout << "  +--------------------------------------------------+\n";
                std::cout << "  |  PID            : " << procInfo.dwProcessId << "\n";
                std::cout << "  |  RAM (WorkingSet): " << metrics.workingSetSizeMB << " MB (Peak: " << metrics.peakWorkingSetSizeMB << " MB)\n";
                std::cout << "  |  Açık Handle    : " << metrics.openHandleCount << "\n";
                std::cout << "  |  Thread Sayısı  : " << metrics.threadCount << "\n";
                std::cout << "  +--------------------------------------------------+\n\n";
            }
        });

        // 2. Üye: Shell'deki "sandbox" / "job" komutlarını JobManager'a yönlendiren hook
        // Komut işlendikten sonra sandbox durumuna göre süreç başlatma modunu günceller
        shell.setJobCommandHook([&jobManager, &shell](const std::vector<std::string>& args) -> bool {
            bool handled = jobManager.handleCommand(args);
            // Sandbox açıldıysa süreçler CREATE_SUSPENDED ile başlatılsın ki Job ataması yapılabilsin
            shell.setStartSuspended(jobManager.isEnabled());
            return handled;
        });

        shell.run();
    }
    catch (const std::exception& e) {
        std::cerr << "[FATAL HATA]: " << e.what() << "\n";
        return 1;
    }
    catch (...) {
        std::cerr << "[FATAL HATA]: Bilinmeyen bir sistem hatası oluştu.\n";
        return 1;
    }

    return 0;
}
