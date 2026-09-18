#include "ShellCore.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <direct.h>
#include <iomanip>

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

// Klasor ve icerigini ozyineli (recursive) silme yardimcisi
static bool deleteDirectoryRecursively(const std::string& path) {
    std::string search = path + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string fullPath = path + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            deleteDirectoryRecursively(fullPath);
        } else {
            DeleteFileA(fullPath.c_str());
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return RemoveDirectoryA(path.c_str()) != 0;
}

bool ShellCore::executeBuiltIn(const std::vector<std::string>& args) {
    if (args.empty()) return false;

    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (cmd == "exit" || cmd == "quit") {
        m_running = false;
        return true;
    }

    // Sürücü değiştirme: c:, d:, e: vb.
    if (cmd.length() == 2 && std::isalpha(static_cast<unsigned char>(cmd[0])) && cmd[1] == ':') {
        std::string drivePath = cmd + "\\";
        if (SetCurrentDirectoryA(drivePath.c_str())) {
            std::cout << "Mevcut Dizin: " << getCurrentWorkingDirectory() << "\n";
        } else {
            std::cerr << "[HATA]: Surucu degistirilemedi: " << cmd << "\n";
        }
        return true;
    }

    // cd / chdir : Dizin değiştirme
    if (cmd == "cd" || cmd == "chdir") {
        if (args.size() < 2) {
            std::cout << "Mevcut Dizin: " << getCurrentWorkingDirectory() << "\n";
            return true;
        }

        std::string target;
        size_t startIdx = 1;
        if ((args[1] == "/d" || args[1] == "/D" || args[1] == "-d") && args.size() >= 3) {
            startIdx = 2;
        }

        if (args[startIdx] == "~") {
            char home[MAX_PATH];
            DWORD len = GetEnvironmentVariableA("USERPROFILE", home, MAX_PATH);
            target = (len > 0 && len < MAX_PATH) ? std::string(home) : "C:\\";
        } else {
            target = args[startIdx];
        }

        // 1. Doğrudan hedef ile dene
        if (SetCurrentDirectoryA(target.c_str())) {
            return true;
        }

        // 2. Tırnaksız boşluklu yol yazılmışsa birleştirip dene (örn: cd Custom Visualizer Shell)
        if (args.size() > startIdx + 1) {
            std::string combined = args[startIdx];
            for (size_t i = startIdx + 1; i < args.size(); ++i) {
                combined += " " + args[i];
            }
            if (SetCurrentDirectoryA(combined.c_str())) {
                return true;
            }
        }

        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            std::cerr << "[cd HATA]: Dizin bulunamadi: " << target << "\n";
            if (args.size() > 2) {
                std::cerr << "  Ipucu: Yol bosluk iceriyorsa tirnak kullanabilirsiniz: cd \"Klasor Adi\"\n";
            }
        } else {
            std::cerr << "[cd HATA]: Dizin degistirilemedi! Hata Kodu: " << err << "\n";
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

    // ─── Dosya Sistemi Komutlari (Win32 API ile dogrudan) ───────────────

    // mkdir / md : Klasor olusturma (CreateDirectoryA)
    if (cmd == "mkdir" || cmd == "md") {
        if (args.size() < 2) {
            std::cerr << "[mkdir HATA]: Klasor adi belirtilmedi.\n";
            std::cerr << "  Kullanim: mkdir <klasor_adi>\n";
        } else {
            std::string dirName = args[1];
            // Tırnaksız boşluklu isim yazıldıysa birleştir
            if (args.size() > 2) {
                for (size_t i = 2; i < args.size(); ++i) dirName += " " + args[i];
            }

            if (CreateDirectoryA(dirName.c_str(), NULL)) {
                std::cout << "  Klasor olusturuldu: " << dirName << "\n";
            } else {
                DWORD err = GetLastError();
                if (err == ERROR_ALREADY_EXISTS) {
                    std::cerr << "[mkdir HATA]: Klasor zaten mevcut: " << dirName << "\n";
                } else {
                    std::cerr << "[mkdir HATA]: Klasor olusturulamadi! Hata Kodu: " << err << "\n";
                }
            }
        }
        return true;
    }

    // rmdir / rd : Klasor silme (RemoveDirectoryA / recursive)
    if (cmd == "rmdir" || cmd == "rd") {
        if (args.size() < 2) {
            std::cerr << "[rmdir HATA]: Klasor adi belirtilmedi.\n";
            std::cerr << "  Kullanim: rmdir <klasor_adi>  veya  rmdir /s <klasor_adi>\n";
        } else {
            bool recursive = false;
            std::string targetDir;
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "/s" || args[i] == "/S" || args[i] == "-r" || args[i] == "-rf") {
                    recursive = true;
                } else if (args[i] != "/q" && args[i] != "/Q") {
                    if (targetDir.empty()) targetDir = args[i];
                    else targetDir += " " + args[i];
                }
            }

            if (targetDir.empty()) {
                std::cerr << "[rmdir HATA]: Silinecek klasor adi belirtilmedi.\n";
            } else if (recursive) {
                if (deleteDirectoryRecursively(targetDir)) {
                    std::cout << "  Klasor ve icerigi silindi: " << targetDir << "\n";
                } else {
                    std::cerr << "[rmdir HATA]: Klasor silinemedi! Hata Kodu: " << GetLastError() << "\n";
                }
            } else {
                if (RemoveDirectoryA(targetDir.c_str())) {
                    std::cout << "  Klasor silindi: " << targetDir << "\n";
                } else {
                    DWORD err = GetLastError();
                    if (err == ERROR_DIR_NOT_EMPTY) {
                        std::cerr << "[rmdir HATA]: Klasor bos degil: " << targetDir << "\n";
                        std::cerr << "  Icerigiyle birlikte silmek icin: rmdir /s " << targetDir << "\n";
                    } else if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                        std::cerr << "[rmdir HATA]: Klasor bulunamadi: " << targetDir << "\n";
                    } else {
                        std::cerr << "[rmdir HATA]: Klasor silinemedi! Hata Kodu: " << err << "\n";
                    }
                }
            }
        }
        return true;
    }

    // del / delete / rm : Dosya silme (DeleteFileA)
    if (cmd == "del" || cmd == "delete" || cmd == "erase" || cmd == "rm") {
        if (args.size() < 2) {
            std::cerr << "[del HATA]: Dosya adi belirtilmedi.\n";
            std::cerr << "  Kullanim: del <dosya_adi>\n";
        } else {
            for (size_t i = 1; i < args.size(); ++i) {
                if (DeleteFileA(args[i].c_str())) {
                    std::cout << "  Dosya silindi: " << args[i] << "\n";
                } else {
                    DWORD err = GetLastError();
                    if (err == ERROR_FILE_NOT_FOUND) {
                        std::cerr << "[del HATA]: Dosya bulunamadi: " << args[i] << "\n";
                    } else {
                        std::cerr << "[del HATA]: Dosya silinemedi! Hata Kodu: " << err << "\n";
                    }
                }
            }
        }
        return true;
    }

    // dir / ls : Klasor icerigini listeleme (FindFirstFileA / FindNextFileA)
    if (cmd == "dir" || cmd == "ls") {
        std::string searchPath;
        if (args.size() >= 2) {
            searchPath = args[1] + "\\*";
        } else {
            searchPath = getCurrentWorkingDirectory() + "\\*";
        }

        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE) {
            std::cerr << "[dir HATA]: Dizin listelenemedi! Hata Kodu: " << GetLastError() << "\n";
        } else {
            std::cout << "\n  Dizin: " << getCurrentWorkingDirectory() << "\n";
            std::cout << "  " << std::string(55, '-') << "\n";

            DWORD fileCount = 0, dirCount = 0;
            ULONGLONG totalSize = 0;

            do {
                bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                ULONGLONG fileSize = (static_cast<ULONGLONG>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;

                // Tarih bilgisi
                SYSTEMTIME stUTC, stLocal;
                FileTimeToSystemTime(&findData.ftLastWriteTime, &stUTC);
                SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

                std::cout << "  " << std::setfill('0')
                          << std::setw(2) << stLocal.wDay << "."
                          << std::setw(2) << stLocal.wMonth << "."
                          << stLocal.wYear << "  "
                          << std::setw(2) << stLocal.wHour << ":"
                          << std::setw(2) << stLocal.wMinute << "  ";

                if (isDir) {
                    std::cout << "  <DIR>         ";
                    dirCount++;
                } else {
                    std::cout << std::setfill(' ') << std::setw(14) << fileSize << " ";
                    totalSize += fileSize;
                    fileCount++;
                }

                std::cout << findData.cFileName << "\n";

            } while (FindNextFileA(hFind, &findData));

            FindClose(hFind);

            std::cout << "  " << std::string(55, '-') << "\n";
            std::cout << "  " << fileCount << " dosya, " << dirCount << " klasor";
            if (totalSize > 0) {
                if (totalSize > 1024 * 1024) {
                    std::cout << "  (Toplam: " << (totalSize / (1024 * 1024)) << " MB)";
                } else if (totalSize > 1024) {
                    std::cout << "  (Toplam: " << (totalSize / 1024) << " KB)";
                } else {
                    std::cout << "  (Toplam: " << totalSize << " Byte)";
                }
            }
            std::cout << "\n\n";
        }
        return true;
    }

    // echo : Ekrana metin yazma
    if (cmd == "echo") {
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) std::cout << " ";
            std::cout << args[i];
        }
        std::cout << "\n";
        return true;
    }

    // type / cat : Dosya icerigini ekrana yazma (CreateFileA / ReadFile)
    if (cmd == "type" || cmd == "cat") {
        if (args.size() < 2) {
            std::cerr << "[type HATA]: Dosya adi belirtilmedi.\n";
            std::cerr << "  Kullanim: type <dosya_adi>\n";
        } else {
            HANDLE hFile = CreateFileA(
                args[1].c_str(), GENERIC_READ, FILE_SHARE_READ,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
            );
            if (hFile == INVALID_HANDLE_VALUE) {
                DWORD err = GetLastError();
                if (err == ERROR_FILE_NOT_FOUND) {
                    std::cerr << "[type HATA]: Dosya bulunamadi: " << args[1] << "\n";
                } else {
                    std::cerr << "[type HATA]: Dosya acilamadi! Hata Kodu: " << err << "\n";
                }
            } else {
                char buffer[4096];
                DWORD bytesRead;
                std::cout << "\n";
                while (ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    std::cout << buffer;
                }
                std::cout << "\n";
                CloseHandle(hFile);
            }
        }
        return true;
    }

    // copy / cp : Dosya kopyalama (CopyFileA)
    if (cmd == "copy" || cmd == "cp") {
        if (args.size() < 3) {
            std::cerr << "[copy HATA]: Kaynak ve hedef belirtilmedi.\n";
            std::cerr << "  Kullanim: copy <kaynak> <hedef>\n";
        } else {
            if (CopyFileA(args[1].c_str(), args[2].c_str(), FALSE)) {
                std::cout << "  Dosya kopyalandi: " << args[1] << " -> " << args[2] << "\n";
            } else {
                DWORD err = GetLastError();
                if (err == ERROR_FILE_NOT_FOUND) {
                    std::cerr << "[copy HATA]: Kaynak dosya bulunamadi: " << args[1] << "\n";
                } else {
                    std::cerr << "[copy HATA]: Kopyalama basarisiz! Hata Kodu: " << err << "\n";
                }
            }
        }
        return true;
    }

    // move / mv / rename / ren : Dosya tasima veya yeniden adlandirma (MoveFileA)
    if (cmd == "move" || cmd == "mv" || cmd == "rename" || cmd == "ren") {
        if (args.size() < 3) {
            std::cerr << "[move HATA]: Kaynak ve hedef belirtilmedi.\n";
            std::cerr << "  Kullanim: move <kaynak> <hedef>\n";
        } else {
            if (MoveFileA(args[1].c_str(), args[2].c_str())) {
                std::cout << "  Dosya tasindi: " << args[1] << " -> " << args[2] << "\n";
            } else {
                DWORD err = GetLastError();
                if (err == ERROR_FILE_NOT_FOUND) {
                    std::cerr << "[move HATA]: Kaynak bulunamadi: " << args[1] << "\n";
                } else {
                    std::cerr << "[move HATA]: Tasima basarisiz! Hata Kodu: " << err << "\n";
                }
            }
        }
        return true;
    }

    // touch : Bos dosya olusturma (CreateFileA)
    if (cmd == "touch") {
        if (args.size() < 2) {
            std::cerr << "[touch HATA]: Dosya adi belirtilmedi.\n";
            std::cerr << "  Kullanim: touch <dosya_adi>\n";
        } else {
            HANDLE hFile = CreateFileA(
                args[1].c_str(), GENERIC_WRITE, 0,
                NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL
            );
            if (hFile == INVALID_HANDLE_VALUE) {
                DWORD err = GetLastError();
                if (err == ERROR_FILE_EXISTS) {
                    std::cout << "  Dosya zaten mevcut: " << args[1] << "\n";
                } else {
                    std::cerr << "[touch HATA]: Dosya olusturulamadi! Hata Kodu: " << err << "\n";
                }
            } else {
                std::cout << "  Dosya olusturuldu: " << args[1] << "\n";
                CloseHandle(hFile);
            }
        }
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
    std::cout << "    cd <path>        : Calisma dizinini degistirir (surucu icin: c:, d:).\n";
    std::cout << "    cls / clear      : Ekrani temizler.\n";
    std::cout << "    help             : Bu yardim menusunu gosterir.\n";
    std::cout << "    exit / quit      : Terminalden cikis yapar.\n\n";
    std::cout << "  Dosya Sistemi Komutlari (Win32 API ile):\n";
    std::cout << "    dir / ls         : Klasor icerigini listeler.\n";
    std::cout << "    mkdir / md <ad>  : Yeni klasor olusturur.\n";
    std::cout << "    rmdir / rd <ad>  : Klasor siler (icerigiyle silmek icin: rmdir /s <ad>).\n";
    std::cout << "    touch <ad>       : Bos dosya olusturur.\n";
    std::cout << "    del / rm <dosya> : Dosya siler.\n";
    std::cout << "    type <dosya>     : Dosya icerigini gosterir.\n";
    std::cout << "    copy <src> <dst> : Dosya kopyalar.\n";
    std::cout << "    move <src> <dst> : Dosya tasir / yeniden adlandirir.\n";
    std::cout << "    echo <metin>     : Ekrana metin yazar.\n\n";
    std::cout << "  Sandbox Komutlari (2. Uye - JobManager):\n";
    std::cout << "    sandbox on/off       : Sandbox modunu ac/kapat.\n";
    std::cout << "    sandbox set ram <MB> : RAM limitini ayarlar.\n";
    std::cout << "    sandbox set cpu <%>  : CPU limitini ayarlar.\n";
    std::cout << "    sandbox status       : Sandbox durumunu gosterir.\n\n";
    std::cout << "  Harici Komutlar (External Process):\n";
    std::cout << "    ping, ipconfig, powershell gibi sistem uygulamalarini\n";
    std::cout << "    CreateProcessA ile alt surec olarak calistirir.\n\n";
    std::cout << "  Moduler Mimari (4 Uye Yapisi):\n";
    std::cout << "    [1] ShellCore   : REPL, Tokenizer, Process & Pipe (Aktif)\n";
    std::cout << "    [2] JobManager  : Job Objects ile RAM/CPU Limitleri (Aktif)\n";
    std::cout << "    [3] Profiler    : Canli CPU/RAM & Handle Analizi (Gelecek)\n";
    std::cout << "    [4] UIEngine    : Split-Screen TUI Paneli (Gelecek)\n";
    std::cout << "=======================================================================\n\n";
}
