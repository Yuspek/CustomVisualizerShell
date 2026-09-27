#ifndef SHELL_CORE_H
#define SHELL_CORE_H

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <iostream>

// ============================================================================
// ProcessInfo Yapısı
// Süreç başlatıldığında oluşan Win32 Handle'larını, PID ve çıktıları tutar.
// ShellCore bu yapıyı doldurur; JobManager ve Profiler modülleri bu veriyi kullanır.
// ============================================================================
struct ProcessInfo {
    HANDLE hProcess = NULL;         // Süreç (Process) Handle'ı
    HANDLE hThread = NULL;          // Ana İş Parçacığı (Thread) Handle'ı
    DWORD dwProcessId = 0;          // Süreç Kimliği (PID)
    DWORD dwThreadId = 0;           // Thread Kimliği (TID)
    DWORD exitCode = 0;             // Sürecin çıkış kodu
    bool success = false;           // Süreç başarıyla başlatıldı mı?
    std::string stdOutput;          // Win32 Anonymous Pipe ile yakalanan ana çıktı (STDOUT)
    std::string stdError;           // Win32 Anonymous Pipe ile yakalanan hata çıktısı (STDERR)
};

// ============================================================================
// ShellCore Sınıfı (1. Üye Görevi)
// REPL döngüsü, komut ayrıştırma, dahili komutlar ve CreateProcessA yönetimi.
// ============================================================================
class ShellCore {
public:
    // Süreç oluşturulduğunda diğer modüllerin tetiklenmesi için callback tanımı
    using ProcessCreatedCallback = std::function<void(ProcessInfo&)>;

    ShellCore();
    ~ShellCore();

    // REPL (Read-Eval-Print Loop) ana döngüsünü çalıştırır
    void run();

    // Kullanıcıdan gelen komut satırını tırnaklara ve boşluklara göre kelimelere ayırır
    static std::vector<std::string> parseCommand(const std::string& input);

    // Dahili komutları (cd, cls, help, sysinfo, ps, kill, history vb.) çalıştırır
    bool executeBuiltIn(const std::vector<std::string>& args);

    // Win32 CreateProcessA ve Pipe boruları ile harici uygulamaları alt süreç olarak başlatır
    ProcessInfo launchProcess(const std::string& commandLine, bool startSuspended = false);

    // JobManager ve Profiler için süreç başlatma callback'ini atar
    void setProcessCreatedHook(ProcessCreatedCallback callback);

    // JobManager modülünün sandbox komutlarını işlemesi için callback atar
    using JobCommandCallback = std::function<bool(const std::vector<std::string>&)>;
    void setJobCommandHook(JobCommandCallback callback);

    // Harici süreçlerin askıda (CREATE_SUSPENDED) başlatılıp başlatılmayacağını ayarlar
    void setStartSuspended(bool suspended);

    // Mevcut çalışma dizinini döndürür
    std::string getCurrentWorkingDirectory() const;

private:
    bool m_running;                         // REPL döngüsü çalışıyor mu?
    ProcessCreatedCallback m_onProcessCreated; // Süreç oluşturma tetikleyicisi
    JobCommandCallback m_onJobCommand;      // Sandbox komut tetikleyicisi
    bool m_startSuspended;                  // Süreç askıda başlatılsın mı?
    HANDLE m_hConsole;                      // Konsol pencere handle'ı
    std::vector<std::string> m_history;     // Komut geçmişi listesi

    // Pipe borusundan gelen veriyi string olarak okur
    std::string readFromPipe(HANDLE hReadPipe);

    // Konsol ekranını temizler
    void clearConsole();

    // Yardım menüsünü ekrana basar
    void printHelp() const;

    // Konsol penceresini (renk, boyut, font, UTF-8) yapılandırır
    void initConsoleWindow();

    // Sistem donanım ve bellek özetini gösterir (sysinfo)
    void printSysInfo() const;

    // Çalışan süreçleri ve PID'leri listeler (ps)
    void printProcessList() const;

    // Belirtilen PID'li süreci sonlandırır (kill)
    void killProcessByPID(DWORD pid) const;

    // Komut geçmişini listeler (history)
    void printHistory() const;

    // Çalıştırılan komutları specter_audit.log dosyasına kaydeder
    void writeAuditLog(const std::string& entry) const;
};

#endif // SHELL_CORE_H
