#include "JobManager.h"
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace OSVisualizer {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor & Destructor
// ─────────────────────────────────────────────────────────────────────────────

JobManager::JobManager() : m_enabled(false) {
    // Varsayılan konfigürasyon SandboxConfig struct'ında tanımlı (256 MB, %50 CPU)
}

JobManager::~JobManager() {
    // Job handle'lar süreç bazlı oluşturulup kapatıldığı için burada ek temizlik gerekmez.
}

// ─────────────────────────────────────────────────────────────────────────────
// Ana İşlev: Job Object Oluşturma ve Sürece Atama
// ─────────────────────────────────────────────────────────────────────────────

SandboxResult JobManager::createJobAndApply(ProcessInfo& procInfo) {
    SandboxResult result;

    // Sandbox modu kapalıysa hiçbir işlem yapma
    if (!m_enabled) {
        result.success = true;
        return result;
    }

    // Geçerli süreç handle'ı kontrolü
    if (procInfo.hProcess == NULL || procInfo.hProcess == INVALID_HANDLE_VALUE) {
        result.success = false;
        result.errorMessage = "Gecersiz surec handle'i. Job Object atanamaz.";
        return result;
    }

    // ─── Adım 1: Anonim Job Object Oluşturma ───────────────────────────────
    // Win32 API: CreateJobObjectA
    // İlk parametre NULL = varsayılan güvenlik öznitelikleri
    // İkinci parametre NULL = anonim (isimsiz) Job Object
    HANDLE hJob = CreateJobObjectA(NULL, NULL);
    if (hJob == NULL) {
        DWORD dwErr = GetLastError();
        result.success = false;
        result.errorMessage = "CreateJobObjectA basarisiz! " + formatWin32Error(dwErr);
        return result;
    }

    // ─── Adım 2: RAM (Bellek) Limiti Ayarlama ──────────────────────────────
    // Win32 API: SetInformationJobObject ile JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    //
    // Kullanılan Flag'ler:
    //   JOB_OBJECT_LIMIT_PROCESS_MEMORY : Tek bir sürecin kullanabileceği max bellek
    //   JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE : Job handle kapatılınca süreç otomatik sonlansın
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION extLimitInfo;
    ZeroMemory(&extLimitInfo, sizeof(extLimitInfo));

    extLimitInfo.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY |     // RAM kısıtlaması aktif
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;   // Job kapatılınca süreci öldür

    // MB → Byte dönüşümü (1 MB = 1024 * 1024 Byte)
    extLimitInfo.ProcessMemoryLimit = static_cast<SIZE_T>(m_config.maxMemoryMB) * 1024ULL * 1024ULL;

    BOOL bResult = SetInformationJobObject(
        hJob,
        JobObjectExtendedLimitInformation,
        &extLimitInfo,
        sizeof(extLimitInfo)
    );

    if (!bResult) {
        DWORD dwErr = GetLastError();
        result.success = false;
        result.errorMessage = "RAM limiti uygulanamadi (SetInformationJobObject)! " + formatWin32Error(dwErr);
        CloseHandle(hJob);
        return result;
    }

    // ─── Adım 3: CPU Limiti Ayarlama ────────────────────────────────────────
    // Win32 API: SetInformationJobObject ile JOBOBJECT_CPU_RATE_CONTROL_INFORMATION
    //
    // Kullanılan Flag'ler:
    //   JOB_OBJECT_CPU_RATE_CONTROL_ENABLE   : CPU hız kontrolünü etkinleştir
    //   JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP : Kesin üst sınır (hard cap) uygula
    //
    // CpuRate değeri: Yüzde × 100 formatında (örn. %25 → 2500, %100 → 10000)
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuRateInfo;
    ZeroMemory(&cpuRateInfo, sizeof(cpuRateInfo));

    cpuRateInfo.ControlFlags =
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |    // CPU kontrolü aktif
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;   // Kesin üst sınır

    // Yüzdeyi Win32 formatına çevir: %25 → 2500
    cpuRateInfo.CpuRate = m_config.maxCpuPercent * 100;

    bResult = SetInformationJobObject(
        hJob,
        JobObjectCpuRateControlInformation,
        &cpuRateInfo,
        sizeof(cpuRateInfo)
    );

    if (!bResult) {
        DWORD dwErr = GetLastError();
        // CPU rate kontrolü bazı Windows sürümlerinde desteklenmeyebilir
        // Bu durumda uyarı verilir ama işleme devam edilir
        std::cerr << "[JobManager UYARI]: CPU limiti uygulanamadi. "
                  << formatWin32Error(dwErr)
                  << " (RAM limiti hala aktif)\n";
    }

    // ─── Adım 4: Süreci Job Object'e Atama ─────────────────────────────────
    // Win32 API: AssignProcessToJobObject
    // Süreç CREATE_SUSPENDED ile başlatıldığı için henüz çalışmıyor.
    // Job atandıktan sonra ShellCore ResumeThread ile süreci devam ettirecek.
    bResult = AssignProcessToJobObject(hJob, procInfo.hProcess);

    if (!bResult) {
        DWORD dwErr = GetLastError();
        result.success = false;
        result.errorMessage = "Surec Job Object'e atanamadi (AssignProcessToJobObject)! " + formatWin32Error(dwErr);
        CloseHandle(hJob);
        return result;
    }

    // ─── Başarılı: Sandbox Raporu Yazdır ────────────────────────────────────
    std::cout << "\n  +--------------------------------------------------+\n";
    std::cout << "  |          SANDBOX AKTIF - Job Object Atandi         |\n";
    std::cout << "  +--------------------------------------------------+\n";
    std::cout << "  |  PID         : " << std::setw(10) << procInfo.dwProcessId << "                       |\n";
    std::cout << "  |  RAM Limiti  : " << std::setw(10) << m_config.maxMemoryMB << " MB" << "                    |\n";
    std::cout << "  |  CPU Limiti  : " << std::setw(10) << m_config.maxCpuPercent << " %" << "                     |\n";
    std::cout << "  |  Kill-on-Close: AKTIF (Job kapaninca surec biter) |\n";
    std::cout << "  +--------------------------------------------------+\n\n";

    result.success = true;
    result.hJob = hJob;

    // Not: hJob burada kapatılmıyor çünkü süreç çalışırken Job'un aktif kalması gerekiyor.
    // Job handle'ı süreç tamamlandığında otomatik olarak geçersizleşecektir
    // (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE sayesinde).
    // Ancak bellek sızıntısını önlemek için CloseHandle çağrısı yapılabilir;
    // bu durumda süreç zaten çalışmaya devam eder, sınırlar aktif kalır.

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Konfigürasyon Getter/Setter'lar
// ─────────────────────────────────────────────────────────────────────────────

void JobManager::setConfig(const SandboxConfig& cfg) {
    m_config = cfg;
}

SandboxConfig JobManager::getConfig() const {
    return m_config;
}

void JobManager::setEnabled(bool enabled) {
    m_enabled = enabled;
}

bool JobManager::isEnabled() const {
    return m_enabled;
}

// ─────────────────────────────────────────────────────────────────────────────
// Durum Raporu
// ─────────────────────────────────────────────────────────────────────────────

std::string JobManager::getStatusReport() const {
    std::ostringstream oss;

    oss << "\n  +--------------------------------------------------+\n";
    oss << "  |            SANDBOX DURUM RAPORU                   |\n";
    oss << "  +--------------------------------------------------+\n";
    oss << "  |  Durum       : " << (m_enabled ? "AKTIF   " : "KAPALI  ") << "                       |\n";
    oss << "  |  RAM Limiti  : " << std::setw(6) << m_config.maxMemoryMB << " MB" << "                        |\n";
    oss << "  |  CPU Limiti  : " << std::setw(6) << m_config.maxCpuPercent << " %" << "                         |\n";
    oss << "  +--------------------------------------------------+\n";
    oss << "  |  Kullanim:                                       |\n";
    oss << "  |    sandbox on           - Sandbox'u aktiflestirir |\n";
    oss << "  |    sandbox off          - Sandbox'u kapatir      |\n";
    oss << "  |    sandbox set ram <MB> - RAM limitini ayarlar    |\n";
    oss << "  |    sandbox set cpu <%>  - CPU limitini ayarlar    |\n";
    oss << "  |    sandbox status       - Bu raporu gosterir     |\n";
    oss << "  +--------------------------------------------------+\n\n";

    return oss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shell Komut İşleyicisi
// ─────────────────────────────────────────────────────────────────────────────

bool JobManager::handleCommand(const std::vector<std::string>& args) {
    // args[0] = "sandbox" veya "job"
    if (args.size() < 2) {
        // Argüman verilmediyse durum raporunu göster
        std::cout << getStatusReport();
        return true;
    }

    // Alt komutu küçük harfe çevir
    std::string subCmd = args[1];
    std::transform(subCmd.begin(), subCmd.end(), subCmd.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // ─── sandbox on ─────────────────────────────────────────────────────────
    if (subCmd == "on") {
        m_enabled = true;
        std::cout << "\n  [JobManager] Sandbox modu AKTIF edildi.\n";
        std::cout << "  Bundan sonra baslatilan tum surecler Job Object ile kisitlanacak.\n";
        std::cout << "  Mevcut ayarlar: RAM=" << m_config.maxMemoryMB << " MB, CPU=%" << m_config.maxCpuPercent << "\n\n";
        return true;
    }

    // ─── sandbox off ────────────────────────────────────────────────────────
    if (subCmd == "off") {
        m_enabled = false;
        std::cout << "\n  [JobManager] Sandbox modu KAPALI.\n";
        std::cout << "  Surecler artik kisitlama olmadan baslatilacak.\n\n";
        return true;
    }

    // ─── sandbox status ─────────────────────────────────────────────────────
    if (subCmd == "status") {
        std::cout << getStatusReport();
        return true;
    }

    // ─── sandbox set ram <MB> / sandbox set cpu <%> ─────────────────────────
    if (subCmd == "set") {
        if (args.size() < 4) {
            std::cerr << "\n  [JobManager HATA]: Eksik parametre.\n";
            std::cerr << "  Kullanim: sandbox set ram <MB>  veya  sandbox set cpu <%>\n\n";
            return true;
        }

        std::string param = args[2];
        std::transform(param.begin(), param.end(), param.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        try {
            unsigned long value = std::stoul(args[3]);

            if (param == "ram" || param == "memory" || param == "mem") {
                if (value < 1 || value > 65536) {
                    std::cerr << "\n  [JobManager HATA]: RAM limiti 1-65536 MB arasinda olmali.\n\n";
                    return true;
                }
                m_config.maxMemoryMB = static_cast<size_t>(value);
                std::cout << "\n  [JobManager] RAM limiti " << m_config.maxMemoryMB << " MB olarak ayarlandi.\n\n";
                return true;
            }

            if (param == "cpu") {
                if (value < 1 || value > 100) {
                    std::cerr << "\n  [JobManager HATA]: CPU limiti 1-100 arasinda olmali.\n\n";
                    return true;
                }
                m_config.maxCpuPercent = static_cast<DWORD>(value);
                std::cout << "\n  [JobManager] CPU limiti %" << m_config.maxCpuPercent << " olarak ayarlandi.\n\n";
                return true;
            }

            std::cerr << "\n  [JobManager HATA]: Bilinmeyen parametre '" << args[2] << "'.\n";
            std::cerr << "  Kullanim: sandbox set ram <MB>  veya  sandbox set cpu <%>\n\n";

        } catch (const std::exception&) {
            std::cerr << "\n  [JobManager HATA]: Gecersiz sayi degeri: '" << args[3] << "'\n\n";
        }

        return true;
    }

    // Tanınmayan alt komut
    std::cerr << "\n  [JobManager HATA]: Bilinmeyen komut '" << args[1] << "'.\n";
    std::cerr << "  'sandbox status' yazarak kullanim bilgilerini gorebilirsiniz.\n\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Win32 Hata Mesajı Formatlayıcı
// ─────────────────────────────────────────────────────────────────────────────

std::string JobManager::formatWin32Error(DWORD dwError) {
    LPSTR messageBuffer = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dwError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer,
        0,
        NULL
    );

    std::string message;
    if (size > 0 && messageBuffer != nullptr) {
        message = std::string(messageBuffer, size);
        // Satır sonu karakterlerini temizle
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
        message = "Hata Kodu " + std::to_string(dwError) + ": " + message;
        LocalFree(messageBuffer);
    } else {
        message = "Hata Kodu " + std::to_string(dwError) + ": Bilinmeyen Win32 hatasi.";
    }

    return message;
}

} // namespace OSVisualizer
