#include "ShellCore.h"
#include "JobManager.h"
#include "Profiler.h"
#include "UIEngine.h"
#include <iostream>
#include <iomanip>

int main() {
    // 1. İkinci harici konsol penceresini gizle (Tek pencere birleşik düzen)
    HWND hConsole = GetConsoleWindow();
    if (hConsole != NULL && IsWindow(hConsole)) {
        ShowWindow(hConsole, SW_HIDE);
    }

    try {
        ShellCore shell;
        OSVisualizer::JobManager jobManager;
        OSVisualizer::Profiler profiler;
        SpecTer::UIEngine uiEngine;

        // 4. Üye <-> 2. Üye: GUI panelindeki slider ve butonlardan gelen Sandbox güncelleme callback'i
        uiEngine.setSandboxUpdateCallback([&jobManager, &shell](const OSVisualizer::SandboxConfig& cfg, bool enabled) {
            jobManager.setConfig(cfg);
            jobManager.setEnabled(enabled);
            shell.setStartSuspended(enabled);
        });

        // 1, 2, 3 ve 4. Üye Entegrasyonu: Süreç başlatıldığında Sandboxing, Profiling ve GUI Güncellemesi
        shell.setProcessCreatedHook([&jobManager, &profiler, &uiEngine](ProcessInfo& procInfo) {
            // 2. Üye: JobManager Sandbox Kısıtlamaları
            if (jobManager.isEnabled()) {
                OSVisualizer::SandboxResult result = jobManager.createJobAndApply(procInfo);
                if (!result.success) {
                    uiEngine.printTerminalText("[JobManager HATA]: " + result.errorMessage + "\r\n");
                }
            }

            // 3. Üye: Profiler Canlı Süreç Metrikleri
            OSVisualizer::ProcessMetrics metrics;
            if (procInfo.hProcess != NULL) {
                metrics = profiler.sampleProcess(procInfo.hProcess);
                uiEngine.setActiveProcess(procInfo.hProcess, procInfo.dwProcessId, "Harici Süreç");
            }

            // 4. Üye: Grafiksel GUI Dashboard Paneli Güncellemesi
            SpecTer::DashboardState state;
            state.metrics = metrics;
            state.sandboxConfig = jobManager.getConfig();
            state.sandboxEnabled = jobManager.isEnabled();
            state.activePID = procInfo.dwProcessId;
            state.processName = procInfo.success ? "Active Process" : "None";
            uiEngine.updateDashboard(state);
        });

        // 2. Üye: Shell'deki "sandbox" / "job" komutlarını JobManager'a yönlendiren hook
        shell.setJobCommandHook([&jobManager, &shell, &uiEngine](const std::vector<std::string>& args) -> bool {
            bool handled = jobManager.handleCommand(args);
            shell.setStartSuspended(jobManager.isEnabled());

            // GUI panel durumunu da güncelle
            SpecTer::DashboardState state;
            state.sandboxConfig = jobManager.getConfig();
            state.sandboxEnabled = jobManager.isEnabled();
            uiEngine.updateDashboard(state);
            return handled;
        });

        // Sol Panele gömülü Terminal'den gelen komutları ShellCore'a bağlama
        uiEngine.setCommandHandler([&shell, &uiEngine](const std::string& cmdLine) {
            shell.executeLineStream(cmdLine, [&uiEngine](const std::string& outputChunk) {
                if (outputChunk == "\x1B[CLEAR]") {
                    uiEngine.clearTerminal();
                } else {
                    uiEngine.printTerminalText(outputChunk);
                }
            });
        });

        // 4. Üye: Birleşik Grafiksel Dashboard ve Gömülü Terminal Penceresini Başlat
        if (uiEngine.initDashboardWindow()) {
            while (uiEngine.isWindowOpen()) {
                Sleep(100);
            }
        }
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
