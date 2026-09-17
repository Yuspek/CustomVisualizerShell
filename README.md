# Custom Visualizer Shell (OS-Visualizer & Sandbox Shell)

Windows İşletim Sistemi kernel seviyesinde süreç yönetimi, canlı metrik izleme ve sandboxing yeteneklerine sahip modüler C++ CLI ve TUI uygulaması.

---

## 🚀 Proje Mimarisi ve Ekip Sorumlulukları

Proje 4 bağımsız ama birbiriyle entegre çalışan modülden oluşmaktadır:

| Modül | Sorumlu Üye | Görevi / İşlevi |
| :--- | :--- | :--- |
| **`ShellCore`** | **1. Üye** | REPL döngüsü, komut ayrıştırma (tokenizer), dahili (`cd`, `cls`, `help`, `exit`) ve harici (`CreateProcessA`) süreç yönetimi, Win32 Pipe ile STDOUT/STDERR yakalama. |
| **`JobManager`** | **2. Üye** | Windows `Job Objects` API (`CreateJobObject`, `AssignProcessToJobObject`) ile süreçlere RAM ve CPU sınırları uygulama. |
| **`Profiler`** | **3. Üye** | `GetProcessMemoryInfo`, `GetProcessTimes` ve Toolhelp32 API ile anlık CPU/RAM kullanımı ve kilitli Handle analiz raporu üretimi. |
| **`UIEngine`** | **4. Üye** | Split-screen (Bölünmüş Ekran) TUI panel çizimi (Sol: Terminal Çıktısı, Sağ: Canlı İzleme & Metrik Paneli). |

---

## 🛠 Derleme ve Çalıştırma (Build & Run)

### Gereksinimler
- **Windows OS** (Native Win32 API)
- **C++17 Uyumlu Derleyici** (MSVC / MinGW-w64 / Clang)
- **CMake 3.16+**

### CMake İle Derleme
```powershell
# 1. Proje kök dizininde build klasörü oluşturun ve yapılandırın
cmake -B build -S .

# 2. Uygulamayı derleyin
cmake --build build --config Release

# 3. Çalıştırın
.\build\Release\CustomVisualizerShell.exe  # MSVC
# veya
.\build\CustomVisualizerShell.exe          # MinGW
```

---

## 📁 Proje Dosya Yapısı

```
CustomVisualizerShell/
├── CMakeLists.txt
├── .gitignore
├── README.md
└── src/
    ├── main.cpp          # Uygulama giriş noktası
    ├── ShellCore.h       # 1. Üye: ShellCore sınıf tanımı & ProcessInfo struct
    ├── ShellCore.cpp     # 1. Üye: REPL, Tokenizer, Win32 Pipe & CreateProcess
    ├── JobManager.h      # 2. Üye: Job Objects & Sandboxing (Stub)
    ├── Profiler.h        # 3. Üye: Metrik İzleme & Handle Scanner (Stub)
    └── UIEngine.h        # 4. Üye: Split-Screen TUI Paneli (Stub)
```

---

## 🤝 Ekip İçi Çalışma Kuralları
1. Her üye kendi header ve kaynak dosyasında (`JobManager`, `Profiler`, `UIEngine`) geliştirmesini yapar.
2. `ShellCore` tarafından üretilen `ProcessInfo` yapısı (`hProcess`, `dwProcessId`) diğer modüller için ana veri kaynağıdır.
3. Koda ekleme yapılmadan önce `git status` ve `git pull` ile güncel kod çekilmeli, ardından `git commit` atılmalıdır.
