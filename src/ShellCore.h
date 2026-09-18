#ifndef SHELL_CORE_H
#define SHELL_CORE_H

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <iostream>

/**
 * @struct ProcessInfo
 * @brief Süreç başlatıldığında Win32 Kernel Handle'ları, PID ve çıktı bilgilerini tutan yapı.
 * 
 * Bu struct, ShellCore tarafından doldurulur ve JobManager (2. Üye) ile
 * Profiler (3. Üye) modüllerine süreç takibi ve kısıtlamaları için aktarılır.
 */
struct ProcessInfo {
    HANDLE hProcess = NULL;         ///< Süreç (Process) Handle'ı
    HANDLE hThread = NULL;          ///< Ana İş Parçacığı (Thread) Handle'ı
    DWORD dwProcessId = 0;          ///< Process ID (PID)
    DWORD dwThreadId = 0;           ///< Thread ID (TID)
    DWORD exitCode = 0;             ///< Sürecin çıkış kodu (Process Exit Code)
    bool success = false;           ///< Süreç başarıyla başlatıldı mı?
    std::string stdOutput;          ///< Win32 Anonymous Pipe ile yakalanan STDOUT
    std::string stdError;           ///< Win32 Anonymous Pipe ile yakalanan STDERR
};

/**
 * @class ShellCore
 * @brief 1. Üye Sorumluluğu: REPL döngüsü, Komut Ayrıştırma, Built-in Yönetimi ve Process Spawning.
 */
class ShellCore {
public:
    using ProcessCreatedCallback = std::function<void(ProcessInfo&)>;

    ShellCore();
    ~ShellCore();

    /**
     * @brief REPL (Read-Eval-Print Loop) ana döngüsünü başlatır.
     */
    void run();

    /**
     * @brief Kullanıcıdan alınan komut satırını tırnak işaretleri ve boşluklara göre tokenlara ayırır.
     * @param input Kullanıcı girdi stringi.
     * @return Argüman listesi (vector of strings).
     */
    static std::vector<std::string> parseCommand(const std::string& input);

    /**
     * @brief Dahili komutları (cd, cls, help, exit) çalıştırır.
     * @param args Ayrıştırılmış komut ve parametreleri.
     * @return Komut dahili bir komut ise ve çalıştırıldıysa true döner.
     */
    bool executeBuiltIn(const std::vector<std::string>& args);

    /**
     * @brief Win32 CreateProcessA API'si ve Pipe boruları ile alt süreci başlatır.
     * @param commandLine Çalıştırılacak tam komut satırı.
     * @param startSuspended JobManager'ın sınır koyabilmesi için sürecin askıda başlatılıp başlatılmayacağı.
     * @return Süreç bilgilerini ve çıktıları içeren ProcessInfo yapısı.
     */
    ProcessInfo launchProcess(const std::string& commandLine, bool startSuspended = false);

    /**
     * @brief JobManager ve Profiler modüllerinin süreç başlatıldığında tetiklenmesi için hook/callback atar.
     */
    void setProcessCreatedHook(ProcessCreatedCallback callback);

    /**
     * @brief JobManager modülünün sandbox komutlarını işlemesi için hook/callback atar.
     * 2. Üye entegrasyonu: sandbox/job komutları bu callback üzerinden yönlendirilir.
     */
    using JobCommandCallback = std::function<bool(const std::vector<std::string>&)>;
    void setJobCommandHook(JobCommandCallback callback);

    /**
     * @brief Harici süreçlerin CREATE_SUSPENDED ile başlatılıp başlatılmayacağını ayarlar.
     * 2. Üye: JobManager sandbox aktifken true yapılmalıdır.
     */
    void setStartSuspended(bool suspended);

    /**
     * @brief Çalışmakta olan güncel dizin yolunu döndürür.
     */
    std::string getCurrentWorkingDirectory() const;

private:
    bool m_running;
    ProcessCreatedCallback m_onProcessCreated;
    JobCommandCallback m_onJobCommand;  ///< 2. Üye: Sandbox komut callback'i
    bool m_startSuspended;              ///< 2. Üye: Süreç askıda başlatılsın mı?

    /**
     * @brief Win32 Pipe handle'ından veriyi güvenli bir şekilde string tamponuna okur.
     */
    std::string readFromPipe(HANDLE hReadPipe);

    /**
     * @brief Terminal ekranını temizleyen Win32 Console API yardımcısı.
     */
    void clearConsole();

    /**
     * @brief Yardım menüsünü ve proje mimarisini ekrana basar.
     */
    void printHelp() const;
};

#endif // SHELL_CORE_H
