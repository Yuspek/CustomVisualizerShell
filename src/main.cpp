#include "ShellCore.h"
#include "JobManager.h"
#include "Profiler.h"
#include "UIEngine.h"
#include "TestSuite.h"
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <string>

// Konsol çıktısını (stdout/stderr) çalışan sürece bağlar (AttachConsole / Pipe)
static void InitConsoleHandles() {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != NULL && hOut != INVALID_HANDLE_VALUE) {
        int fdOut = _open_osfhandle((intptr_t)hOut, _O_TEXT);
        if (fdOut != -1) {
            FILE* fp = _fdopen(fdOut, "w");
            if (fp) {
                *stdout = *fp;
                setvbuf(stdout, NULL, _IONBF, 0);
            }
        }
    } else {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
        }
    }
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != NULL && hErr != INVALID_HANDLE_VALUE) {
        int fdErr = _open_osfhandle((intptr_t)hErr, _O_TEXT);
        if (fdErr != -1) {
            FILE* fp = _fdopen(fdErr, "w");
            if (fp) {
                *stderr = *fp;
                setvbuf(stderr, NULL, _IONBF, 0);
            }
        }
    }
}

// Canlı telemetri grafiği ve JobManager limitlerini test etmek için dahili stres işçisi
static int RunStressWorker(int ramMB, int durationSec, int targetCpu) {
    InitConsoleHandles();

    std::cout << "\n=======================================================\n";
    std::cout << "        🚀 [SpecTer CANLI TELEMETRİ STRES TESTİ]       \n";
    std::cout << "=======================================================\n";
    std::cout << "  Hedef RAM    : " << ramMB << " MB\n";
    std::cout << "  Hedef CPU    : %" << targetCpu << "\n";
    std::cout << "  Test Süresi  : " << durationSec << " Saniye\n";
    std::cout << "  PID          : " << GetCurrentProcessId() << "\n";
    std::cout << "  İptal        : İstediğiniz an 'Ctrl+C' basabilirsiniz\n";
    std::cout << "=======================================================\n\n";
    std::cout.flush();

    // 1. RAM Tahsisi (Sayfaları fiziksel belleğe yazarak Working Set'i doldur)
    size_t totalBytes = static_cast<size_t>(ramMB) * 1024ULL * 1024ULL;
    char* memoryBlock = nullptr;
    try {
        memoryBlock = new char[totalBytes];
        for (size_t i = 0; i < totalBytes; i += 4096) {
            memoryBlock[i] = static_cast<char>(i & 0xFF);
        }
        std::cout << "  [OK] " << ramMB << " MB RAM fiziksel belleğe tahsis edildi.\n";
        std::cout << "  [OK] CPU yükü başlatıldı. Grafiği ve radyal göstergeyi izleyin...\n\n";
        std::cout.flush();
    } catch (...) {
        std::cerr << "  [HATA] " << ramMB << " MB RAM tahsis edilemedi! (JobManager Sandbox limiti aşılmış olabilir)\n\n";
        std::cerr.flush();
        return 1;
    }

    // 2. Kontrollü CPU Yükü Döngüsü
    auto startTime = std::chrono::steady_clock::now();
    int busyMs = targetCpu;
    int sleepMs = 100 - targetCpu;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        if (elapsed >= durationSec) {
            break;
        }

        auto cycleStart = std::chrono::steady_clock::now();
        while (true) {
            auto cycleNow = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(cycleNow - cycleStart).count();
            if (ms >= busyMs) break;

            volatile double dummy = 0.0;
            for (int k = 0; k < 10000; ++k) {
                dummy += k * 0.001;
            }
        }

        if (sleepMs > 0) {
            Sleep(sleepMs);
        }
    }

    std::cout << "  [TAMAMLANDI] " << durationSec << " saniyelik stres testi başarıyla bitti.\n\n";
    std::cout.flush();
    delete[] memoryBlock;
    return 0;
}

// WIN32 SUBSYSTEM: Konsol penceresi hiç oluşturulmaz, doğrudan GUI başlar
int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // Komut satırı argümanlarını kontrol et: --test veya --stress modları tek exe'den çalışır
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argvW && argc > 1) {
        std::wstring arg1 = argvW[1];
        if (arg1 == L"--test" || arg1 == L"-test" || arg1 == L"/test" || arg1 == L"test") {
            InitConsoleHandles();
            int res = RunAllSpecTerTests();
            LocalFree(argvW);
            return res;
        }
        if (arg1 == L"--stress" || arg1 == L"-stress" || arg1 == L"/stress" || arg1 == L"stress") {
            int ramMB = 250;
            int durationSec = 20;
            int targetCpu = 75;
            if (argc > 2) {
                try { ramMB = std::stoi(argvW[2]); } catch (...) {}
            }
            if (argc > 3) {
                try { durationSec = std::stoi(argvW[3]); } catch (...) {}
            }
            if (argc > 4) {
                try { targetCpu = std::stoi(argvW[4]); } catch (...) {}
            }
            LocalFree(argvW);
            return RunStressWorker(ramMB, durationSec, targetCpu);
        }
        LocalFree(argvW);
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
                } else {
                    if (!result.warning.empty()) {
                        uiEngine.printTerminalText(result.warning + "\r\n");
                    }
                    if (!result.report.empty()) {
                        uiEngine.printTerminalText(result.report);
                    }
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
                if (outputChunk == ShellCore::kClearMarker) {
                    uiEngine.clearTerminal();
                } else if (outputChunk == ShellCore::kExitMarker) {
                    uiEngine.closeDashboard();
                } else {
                    uiEngine.printTerminalText(outputChunk);
                }
            });
        });

        // Ctrl+C ile çalışan harici süreci sonlandırma hook'u
        uiEngine.setInterruptHandler([&shell]() {
            shell.interruptCurrentProcess();
        });


        // 4. Üye: Birleşik Grafiksel Dashboard ve Gömülü Terminal Penceresini Başlat
        if (uiEngine.initDashboardWindow()) {
            // Gömülü Terminal'e karşılama mesajı yaz
            uiEngine.printTerminalText(
                "=======================================================================\r\n"
                "  SpecTer v1.0 (Milestone 1-3 Entegre: ShellCore + JobManager + Profiler)\r\n"
                "  İpucu: 'help' yazarak tüm komutları ve modül mimarisini görebilirsiniz.\r\n"
                "=======================================================================\r\n\r\n"
            );
            uiEngine.printPrompt();

            while (uiEngine.isWindowOpen()) {
                Sleep(100);
            }
        }
    }
    catch (const std::exception& e) {
        MessageBoxA(NULL, e.what(), "SpecTer FATAL HATA", MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (...) {
        MessageBoxA(NULL, "Bilinmeyen bir sistem hatası oluştu.", "SpecTer FATAL HATA", MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}
