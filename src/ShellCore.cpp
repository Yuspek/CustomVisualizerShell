#include "ShellCore.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <direct.h>

ShellCore::ShellCore() : m_running(false), m_onProcessCreated(nullptr), m_onJobCommand(nullptr), m_startSuspended(false) {}

ShellCore::~ShellCore() {}

void ShellCore::run() {
    m_running = true;

    std::cout << "=======================================================================\n";
    std::cout << "  OS-Visualizer & Sandbox Shell (Milestone 1: ShellCore Enabled)\n";
    std::cout << "  Tip: 'help' yazarak dahili komutlari ve proje mimarisini gorebilirsiniz.\n";
    std::cout << "=======================================================================\n\n";

    while (m_running) {
        std::string currentDir = getCurrentWorkingDirectory();
        std::cout << "OS-Visualizer [" << currentDir << "]> ";

        std::string inputLine;
        if (!std::getline(std::cin, inputLine)) {
            break; // EOF veya stdin kapandi
        }

        // Bos satirlari atla
        if (inputLine.empty()) {
            continue;
        }

        // Komut satirini tokenlarina ayir
        std::vector<std::string> args = parseCommand(inputLine);
        if (args.empty()) {
            continue;
        }

        // Dahili (Built-in) komut kontrolu
        if (executeBuiltIn(args)) {
            continue;
        }

        // Harici (External) süreç baslatma
        // m_startSuspended: JobManager sandbox aktifken true, hook ile Job atandıktan sonra ResumeThread yapılır
        ProcessInfo procInfo = launchProcess(inputLine, m_startSuspended);

        if (procInfo.success) {
            // STDOUT ciktisini ekrana yazdir
            if (!procInfo.stdOutput.empty()) {
                std::cout << procInfo.stdOutput;
            }

            // STDERR ciktisini ekrana yazdir
            if (!procInfo.stdError.empty()) {
                std::cerr << "[STDERR]: " << procInfo.stdError;
            }

            std::cout << "\n[Process Exited with Code: " << procInfo.exitCode << " | PID: " << procInfo.dwProcessId << "]\n\n";

            // Process & Thread Handle'larini kapat (Resource cleanup)
            if (procInfo.hProcess != NULL) CloseHandle(procInfo.hProcess);
            if (procInfo.hThread != NULL) CloseHandle(procInfo.hThread);
        } else {
            std::cerr << "[ShellCore HATA]: Surec baslatilamadi! " << procInfo.stdError << "\n\n";
        }
    }

    std::cout << "OS-Visualizer Shell kapatiliyor. Hoscakalin!\n";
}

std::vector<std::string> ShellCore::parseCommand(const std::string& input) {
    std::vector<std::string> tokens;
    std::string currentToken;
    bool inDoubleQuotes = false;
    bool inSingleQuotes = false;

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];

        if (c == '"' && !inSingleQuotes) {
            inDoubleQuotes = !inDoubleQuotes;
        } else if (c == '\'' && !inDoubleQuotes) {
            inSingleQuotes = !inSingleQuotes;
        } else if ((c == ' ' || c == '\t') && !inDoubleQuotes && !inSingleQuotes) {
            if (!currentToken.empty()) {
                tokens.push_back(currentToken);
                currentToken.clear();
            }
        } else {
            currentToken += c;
        }
    }

    if (!currentToken.empty()) {
        tokens.push_back(currentToken);
    }

    return tokens;
}

bool ShellCore::executeBuiltIn(const std::vector<std::string>& args) {
    if (args.empty()) return false;

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "exit" || cmd == "quit") {
        m_running = false;
        return true;
    }

    if (cmd == "cd" || cmd == "chdir") {
        if (args.size() < 2) {
            std::cout << "Mevcut Dizin: " << getCurrentWorkingDirectory() << "\n";
        } else {
            if (!SetCurrentDirectoryA(args[1].c_str())) {
                DWORD err = GetLastError();
                std::cerr << "[cd HATA]: Dizin degistirilemedi! Hata Kodu: " << err << "\n";
            }
        }
        return true;
    }

    if (cmd == "cls" || cmd == "clear") {
        clearConsole();
        return true;
    }

    if (cmd == "help") {
        printHelp();
        return true;
    }

    // JobManager komut yönlendirmesi (2. Üye)
    if (cmd == "sandbox" || cmd == "job") {
        if (m_onJobCommand) {
            return m_onJobCommand(args);
        }
    }

    return false;
}

ProcessInfo ShellCore::launchProcess(const std::string& commandLine, bool startSuspended) {
    ProcessInfo info;

    // Win32 Anonymous Pipe Security Attributes
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE; // Alt surec handle'i miras alabilsin
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hChildStdOutRead = NULL;
    HANDLE hChildStdOutWrite = NULL;
    HANDLE hChildStdErrRead = NULL;
    HANDLE hChildStdErrWrite = NULL;

    // STDOUT Pipe Olusturma
    if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &saAttr, 0)) {
        info.stdError = "STDOUT Pipe olusturulamadi.";
        return info;
    }
    SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0); // Okuma ucu miras alinmasin

    // STDERR Pipe Olusturma
    if (!CreatePipe(&hChildStdErrRead, &hChildStdErrWrite, &saAttr, 0)) {
        info.stdError = "STDERR Pipe olusturulamadi.";
        CloseHandle(hChildStdOutRead);
        CloseHandle(hChildStdOutWrite);
        return info;
    }
    SetHandleInformation(hChildStdErrRead, HANDLE_FLAG_INHERIT, 0);

    // Win32 STARTUPINFO yapisi
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = hChildStdOutWrite;
    si.hStdError = hChildStdErrWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    // Win32 CreateProcessA icin komut satiri tamponu (Mutable olmali)
    std::vector<char> cmdBuffer(commandLine.begin(), commandLine.end());
    cmdBuffer.push_back('\0');

    DWORD dwCreationFlags = startSuspended ? CREATE_SUSPENDED : 0;

    // Native Win32 CreateProcessA cagrisi
    BOOL bSuccess = CreateProcessA(
        NULL,                   // Modul adi (NULL -> komut satirindan al)
        cmdBuffer.data(),       // Komut satiri stringi
        NULL,                   // Process Security Attributes
        NULL,                   // Thread Security Attributes
        TRUE,                   // Handle Inheritance (Pipe'lar icin TRUE)
        dwCreationFlags,        // Creation Flags (CREATE_SUSPENDED vb.)
        NULL,                   // Environment Variables (Mevcut ortami kullan)
        NULL,                   // Current Directory (Mevcut dizini kullan)
        &si,                    // Startup Info
        &pi                     // Process Information
    );

    // Ebeveyn süreçteki yazma uçlarını kapatıyoruz ki pipe EOF alabilsin
    CloseHandle(hChildStdOutWrite);
    CloseHandle(hChildStdErrWrite);

    if (!bSuccess) {
        DWORD dwErr = GetLastError();
        LPSTR messageBuffer = nullptr;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, dwErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL
        );

        info.success = false;
        info.stdError = messageBuffer ? std::string(messageBuffer) : "Bilinmeyen Win32 hatasi.";
        if (messageBuffer) LocalFree(messageBuffer);

        CloseHandle(hChildStdOutRead);
        CloseHandle(hChildStdErrRead);
        return info;
    }

    // ProcessInfo doldurma
    info.success = true;
    info.hProcess = pi.hProcess;
    info.hThread = pi.hThread;
    info.dwProcessId = pi.dwProcessId;
    info.dwThreadId = pi.dwThreadId;

    // JobManager & Profiler Hook tetikleme
    if (m_onProcessCreated) {
        m_onProcessCreated(info);
    }

    // Eger askida (suspended) baslatildiysa ve hook tamamlandiysa thread'i devam ettir
    if (startSuspended) {
        ResumeThread(pi.hThread);
    }

    // Pipe'lardan STDOUT ve STDERR okuma
    info.stdOutput = readFromPipe(hChildStdOutRead);
    info.stdError = readFromPipe(hChildStdErrRead);

    // Sürecin tamamlanmasını bekle
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Çıkış kodunu al
    GetExitCodeProcess(pi.hProcess, &info.exitCode);

    // Read Pipe handle'larini kapat
    CloseHandle(hChildStdOutRead);
    CloseHandle(hChildStdErrRead);

    return info;
}

std::string ShellCore::readFromPipe(HANDLE hReadPipe) {
    std::string output;
    char buffer[4096];
    DWORD bytesRead = 0;

    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output.append(buffer, bytesRead);
    }

    return output;
}

void ShellCore::setProcessCreatedHook(ProcessCreatedCallback callback) {
    m_onProcessCreated = callback;
}

void ShellCore::setJobCommandHook(JobCommandCallback callback) {
    m_onJobCommand = callback;
}

void ShellCore::setStartSuspended(bool suspended) {
    m_startSuspended = suspended;
}

std::string ShellCore::getCurrentWorkingDirectory() const {
    char buffer[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, buffer)) {
        return std::string(buffer);
    }
    return std::string(".");
}

void ShellCore::clearConsole() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;
    COORD homeCoords = { 0, 0 };

    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;

    if (!FillConsoleOutputCharacterA(hConsole, (CHAR)' ', cellCount, homeCoords, &count)) return;
    if (!FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, homeCoords, &count)) return;
    SetConsoleCursorPosition(hConsole, homeCoords);
}

void ShellCore::printHelp() const {
    std::cout << "\n=======================================================================\n";
    std::cout << "                 OS-VISUALIZER & SANDBOX SHELL - YARDIM                \n";
    std::cout << "=======================================================================\n";
    std::cout << "  Dahili Komutlar (Built-in Commands):\n";
    std::cout << "    cd <path>        : Calisma dizinini degistirir.\n";
    std::cout << "    cls / clear      : Ekranı temizler.\n";
    std::cout << "    help             : Bu yardım menusunu ve mimariyi gösterir.\n";
    std::cout << "    exit / quit      : Terminalden cıkıs yapar.\n\n";
    std::cout << "  Sistem Komutları (System Commands):\n";
    std::cout << "    'ping google.com', 'dir', 'ipconfig', 'powershell' gibi tum Win32\n";
    std::cout << "    veya sistem uygulamalarını CreateProcessA ile alt surec olarak\n";
    std::cout << "    calıstırabilir ve STDOUT/STDERR cıktılarını pipe ile yakalar.\n\n";
    std::cout << "  Moduler Mimari (4 Uye Yapısı):\n";
    std::cout << "    [1] ShellCore   : REPL, Tokenizer, Process & Pipe Redirection (Aktif)\n";
    std::cout << "    [2] JobManager  : Job Objects ile RAM/CPU Limitleri (Gelecek)\n";
    std::cout << "    [3] Profiler    : Canli CPU/RAM & Handle Analizi (Gelecek)\n";
    std::cout << "    [4] UIEngine    : Split-Screen TUI Paneli (Gelecek)\n";
    std::cout << "=======================================================================\n\n";
}
