#include "ShellCore.h"
#include "JobManager.h"
#include <iostream>

int main() {
    try {
        ShellCore shell;
        OSVisualizer::JobManager jobManager;

        // 2. Üye: Süreç başlatıldığında JobManager'ın sandbox kısıtlamalarını uygulaması için hook
        // ShellCore, CREATE_SUSPENDED ile süreci başlatır → bu callback tetiklenir →
        // JobManager Job Object oluşturup süreci atar → ShellCore ResumeThread ile devam ettirir
        shell.setProcessCreatedHook([&jobManager](ProcessInfo& procInfo) {
            if (jobManager.isEnabled()) {
                OSVisualizer::SandboxResult result = jobManager.createJobAndApply(procInfo);
                if (!result.success) {
                    std::cerr << "[JobManager HATA]: " << result.errorMessage << "\n";
                }
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
