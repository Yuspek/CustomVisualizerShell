#include <windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <string>

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    int ramMB = 250;        // Varsayılan: 250 MB
    int durationSec = 20;   // Varsayılan: 20 Saniye
    int targetCpu = 75;     // Varsayılan: %75 CPU yükü

    if (argc >= 2) {
        try { ramMB = std::stoi(argv[1]); } catch (...) {}
    }
    if (argc >= 3) {
        try { durationSec = std::stoi(argv[2]); } catch (...) {}
    }
    if (argc >= 4) {
        try { targetCpu = std::stoi(argv[3]); } catch (...) {}
    }

    std::cout << "\n=======================================================\n";
    std::cout << "        🚀 [SpecTer CANLI TELEMETRİ STRES TESTİ]       \n";
    std::cout << "=======================================================\n";
    std::cout << "  Hedef RAM    : " << ramMB << " MB\n";
    std::cout << "  Hedef CPU    : %" << targetCpu << "\n";
    std::cout << "  Test Süresi  : " << durationSec << " Saniye\n";
    std::cout << "  PID          : " << GetCurrentProcessId() << "\n";
    std::cout << "  İptal        : İstediğiniz an 'Ctrl+C' basabilirsiniz\n";
    std::cout << "=======================================================\n\n";

    // 1. RAM Tahsisi (Sayfaları fiziksel belleğe yazarak Working Set'i doldur)
    size_t totalBytes = static_cast<size_t>(ramMB) * 1024ULL * 1024ULL;
    char* memoryBlock = nullptr;
    try {
        memoryBlock = new char[totalBytes];
        // Sayfa boyutu (4KB) adımlarla belleğe yaz ki Windows OS Working Set'e alsın
        for (size_t i = 0; i < totalBytes; i += 4096) {
            memoryBlock[i] = static_cast<char>(i & 0xFF);
        }
        std::cout << "  [OK] " << ramMB << " MB RAM fiziksel belleğe tahsis edildi.\n";
        std::cout << "  [OK] CPU yükü başlatıldı. Grafiği ve radyal göstergeyi izleyin...\n\n";
    } catch (...) {
        std::cerr << "  [HATA] " << ramMB << " MB RAM tahsis edilemedi! (JobManager Sandbox limiti aşılmış olabilir)\n\n";
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
    delete[] memoryBlock;
    return 0;
}
