#include "ShellCore.h"
#include <iostream>

int main() {
    try {
        ShellCore shell;
        shell.run();
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
