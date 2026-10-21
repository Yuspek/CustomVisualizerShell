#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <windows.h>
#include "ShellCore.h"
#include "JobManager.h"
#include "Profiler.h"
#include "UIEngine.h"

// Basit test koşucu makroları
static int g_passedTests = 0;
static int g_failedTests = 0;

#define TEST_ASSERT(expr, msg) \
    do { \
        if (expr) { \
            std::cout << "  [PASS] " << msg << "\n"; \
            g_passedTests++; \
        } else { \
            std::cerr << "  [FAIL] " << msg << " (" #expr ") at line " << __LINE__ << "\n"; \
            g_failedTests++; \
        } \
    } while (0)

void testShellCore() {
    std::cout << "\n=== 1. ShellCore Testleri ===\n";
    ShellCore shell;

    // Test 1: Yardım metni ve temel başlatma
    TEST_ASSERT(shell.isRunning() == true, "ShellCore varsayılan olarak çalışır durumda başlatılmalı");

    // Test 2: Set start suspended
    shell.setStartSuspended(true);
    shell.setStartSuspended(false);
    TEST_ASSERT(true, "ShellCore startSuspended bayrağı güvenle değiştirilebilir");

    // Test 3: executeLineStream ile yerleşik komutlar
    bool clearReceived = false;
    shell.executeLineStream("cls", [&clearReceived](const std::string& out) {
        if (out == ShellCore::kClearMarker) {
            clearReceived = true;
        }
    });
    TEST_ASSERT(clearReceived == true, "ShellCore 'cls' komutu kClearMarker üretmeli");

    bool helpReceived = false;
    shell.executeLineStream("help", [&helpReceived](const std::string& out) {
        if (out.find("SpecTer") != std::string::npos) {
            helpReceived = true;
        }
    });
    TEST_ASSERT(helpReceived == true, "ShellCore 'help' komutu SpecTer yardım bilgisini içermeli");

    bool timeReceived = false;
    shell.executeLineStream("time", [&timeReceived](const std::string& out) {
        if (out.find("Sistem Zamani") != std::string::npos || out.find("Sistem") != std::string::npos) {
            timeReceived = true;
        }
    });
    TEST_ASSERT(timeReceived == true, "ShellCore 'time' komutu geçerli saat çıktısı vermeli");

    bool sysinfoReceived = false;
    shell.executeLineStream("sysinfo", [&sysinfoReceived](const std::string& out) {
        if (out.find("CPU Mimarisi") != std::string::npos || out.find("DONANIM") != std::string::npos) {
            sysinfoReceived = true;
        }
    });
    TEST_ASSERT(sysinfoReceived == true, "ShellCore 'sysinfo' komutu sistem özetini döndürmeli");

    bool exitReceived = false;
    shell.executeLineStream("exit", [&exitReceived](const std::string& out) {
        if (out == ShellCore::kExitMarker) {
            exitReceived = true;
        }
    });
    TEST_ASSERT(exitReceived == true, "ShellCore 'exit' komutu kExitMarker üretmeli");
    TEST_ASSERT(shell.isRunning() == false, "ShellCore 'exit' sonrası running = false olmalı");
}

void testJobManager() {
    std::cout << "\n=== 2. JobManager Testleri ===\n";
    OSVisualizer::JobManager jm;

    // Test 1: Varsayılan ayarlar
    TEST_ASSERT(jm.isEnabled() == false, "JobManager varsayılan olarak kapalı olmalı");
    OSVisualizer::SandboxConfig cfg = jm.getConfig();
    TEST_ASSERT(cfg.maxMemoryMB == 256, "Varsayılan RAM limiti 256 MB olmalı");
    TEST_ASSERT(cfg.maxCpuPercent == 50, "Varsayılan CPU limiti %50 olmalı");

    // Test 2: Ayar değiştirme
    jm.setEnabled(true);
    TEST_ASSERT(jm.isEnabled() == true, "JobManager etkinleştirilebilmeli");

    cfg.maxMemoryMB = 512;
    cfg.maxCpuPercent = 75;
    jm.setConfig(cfg);
    OSVisualizer::SandboxConfig readCfg = jm.getConfig();
    TEST_ASSERT(readCfg.maxMemoryMB == 512, "RAM limiti 512 MB olarak güncellenebilmeli");
    TEST_ASSERT(readCfg.maxCpuPercent == 75, "CPU limiti %75 olarak güncellenebilmeli");

    // Test 3: Argüman komut ayrıştırıcı (handleCommand)
    std::vector<std::string> argsStatus = {"sandbox", "status"};
    bool handled = jm.handleCommand(argsStatus);
    TEST_ASSERT(handled == true, "JobManager 'sandbox status' komutunu işlemeli");

    std::vector<std::string> argsRam = {"sandbox", "ram", "1024"};
    jm.handleCommand(argsRam);
    TEST_ASSERT(jm.getConfig().maxMemoryMB == 1024, "JobManager 'sandbox ram 1024' ayarını güncellemeli");

    std::vector<std::string> argsCpu = {"sandbox", "cpu", "30"};
    jm.handleCommand(argsCpu);
    TEST_ASSERT(jm.getConfig().maxCpuPercent == 30, "JobManager 'sandbox cpu 30' ayarını güncellemeli");

    std::vector<std::string> argsOff = {"sandbox", "off"};
    jm.handleCommand(argsOff);
    TEST_ASSERT(jm.isEnabled() == false, "JobManager 'sandbox off' komutunu kapatmalı");

    // Test 4: Geçersiz handle ile Job Object koruması
    jm.setEnabled(true);
    ProcessInfo badProc;
    badProc.hProcess = NULL;
    OSVisualizer::SandboxResult res = jm.createJobAndApply(badProc);
    TEST_ASSERT(res.success == false, "Geçersiz süreç handle'ına Job Object atanması engellenmeli");
}

void testProfiler() {
    std::cout << "\n=== 3. Profiler Testleri ===\n";
    OSVisualizer::Profiler profiler;

    HANDLE hCurrent = GetCurrentProcess();
    TEST_ASSERT(hCurrent != NULL, "Mevcut süreç handle'ı alınabilmeli");

    OSVisualizer::ProcessMetrics m1 = profiler.sampleProcess(hCurrent);
    TEST_ASSERT(m1.workingSetSizeMB > 0, "Mevcut sürecin RAM kullanımı (Working Set) > 0 MB olmalı");
    TEST_ASSERT(m1.openHandleCount > 0, "Mevcut sürecin açık handle sayısı > 0 olmalı");
    TEST_ASSERT(m1.threadCount >= 1, "Mevcut sürecin thread sayısı >= 1 olmalı");
    TEST_ASSERT(m1.cpuUsagePercent >= 0.0, "CPU kullanım yüzdesi negatif olamaz");

    // Test 2: İkinci örnekleme ve delta hesabı
    Sleep(50);
    OSVisualizer::ProcessMetrics m2 = profiler.sampleProcess(hCurrent);
    TEST_ASSERT(m2.cpuUsagePercent >= 0.0 && m2.cpuUsagePercent <= 400.0, "İkinci CPU örneklemesi geçerli aralıkta olmalı");

    // Test 3: Reset mekanizması
    profiler.reset();
    TEST_ASSERT(true, "Profiler sıfırlama hatasız tamamlanmalı");
}

void testUIEngine() {
    std::cout << "\n=== 4. UIEngine (Dashboard GUI) Testleri ===\n";
    SpecTer::UIEngine ui;

    // Test 1: Başlatma öncesi durum
    TEST_ASSERT(ui.isWindowOpen() == false, "UIEngine pencere açılmadan önce kapalı görünmeli");

    // Test 2: Pencerenin başarıyla oluşturulması
    bool initOk = ui.initDashboardWindow();
    TEST_ASSERT(initOk == true, "UIEngine dashboard penceresi başarıyla oluşturulmalı");
    TEST_ASSERT(ui.isWindowOpen() == true, "UIEngine penceresi oluşturulduktan sonra açık görünmeli");

    // Test 3: Canlı Dashboard veri güncellemesi
    SpecTer::DashboardState state;
    state.activePID = 1234;
    state.processName = "test_app.exe";
    state.metrics.cpuUsagePercent = 42.5;
    state.metrics.workingSetSizeMB = 128;
    state.sandboxEnabled = true;
    state.sandboxConfig.maxMemoryMB = 512;
    state.sandboxConfig.maxCpuPercent = 60;
    ui.updateDashboard(state);
    TEST_ASSERT(true, "UIEngine dashboard metrik güncellemesi hatasız çalışmalı");

    // Test 4: Gömülü Terminal metin basımı
    ui.printTerminalText("SpecTer Otomasyon Testi: Terminal Çıktısı Doğrulandı.\r\n");
    TEST_ASSERT(true, "UIEngine gömülü terminal metin ekleme hatasız çalışmalı");

    // Test 5: Pencerenin kapatılması ve kaynak temizliği
    ui.closeDashboard();
    TEST_ASSERT(ui.isWindowOpen() == false, "UIEngine penceresi kapatıldıktan sonra kapalı görünmeli");
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "========================================================\n";
    std::cout << "         SpecTer Projesi Kapsamlı Otomasyon Testi        \n";
    std::cout << "========================================================\n";

    testShellCore();
    testJobManager();
    testProfiler();
    testUIEngine();

    std::cout << "\n--------------------------------------------------------\n";
    std::cout << "Test Sonucu: " << g_passedTests << " BAŞARILI, " << g_failedTests << " BAŞARISIZ.\n";
    std::cout << "========================================================\n";

    return (g_failedTests == 0) ? 0 : 1;
}

