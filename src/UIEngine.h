#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <string>
#include "Profiler.h"

namespace OSVisualizer {

/**
 * @class UIEngine
 * @brief 4. Üye Sorumluluğu: FTXUI veya Console API kullanarak bölünmüş ekran (Split-screen) TUI çizimi.
 */
class UIEngine {
public:
    UIEngine() = default;
    ~UIEngine() = default;

    /**
     * @brief Ekranı ikiye bölerek sol tarafta terminal cıktısını, sağ tarafta canlı metrik panelini çizer.
     */
    void renderSplitScreen(const std::string& stdoutBuffer, const ProcessMetrics& metrics) {
        // 4. Üye buraya TUI render kodlarını ekleyecektir.
        (void)stdoutBuffer;
        (void)metrics;
    }
};

} // namespace OSVisualizer

#endif // UI_ENGINE_H
