#define _POSIX_C_SOURCE 200809L

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>

#include <filament/Color.h>

using filament::Color;
using filament::LinearColor;

/*
 * Filament Color Processor
 * 模擬影像應用：針對多張圖片做色彩溫度與吸收校正。
 *
 * 流程：
 *   1. 模擬多張圖片（每張代表不同的平均色溫）
 *   2. 對每張圖片執行：
 *        - 計算 CCT (correlated color temperature)
 *        - 套用 Illuminant D 校正
 *        - 模擬光線吸收（Beer-Lambert 模型）
 *   3. 將結果輸出成簡易的 RGB 值（假裝是處理後的影像摘要）
 */

struct Image {
    std::string name;
    float temperature;   // Kelvin
    float distance;      // optical path distance
    LinearColor color;   // base color
};

static std::vector<Image> generateImageSet(int count) {
    std::vector<Image> images;
    images.reserve(count);
    for (int i = 0; i < count; ++i) {
        float temp = 2000.0f + i * 50.0f;      // 2000K ~ 15000K
        float dist = 0.5f + 0.0005f * i;       // arbitrary distance
        LinearColor base = {0.8f, 0.7f, 0.6f};
        images.push_back({"image_" + std::to_string(i) + ".jpg", temp, dist, base});
    }
    return images;
}

static void processImages(std::vector<Image>& images, int repeat) {
    for (int iter = 0; iter < repeat; ++iter) {
        for (auto& img : images) {
            auto cct = Color::cct(img.temperature);
            auto illum = Color::illuminantD(std::min(img.temperature + 1000.0f, 25000.0f));
            img.color = Color::absorptionAtDistance(cct * 0.8f + illum * 0.2f, img.distance);
        }
    }
}

// static void exportResults(const std::vector<Image>& images) {
//     printf("=== Processed Image Summary ===\n");
//     for (size_t i = 0; i < std::min<size_t>(images.size(), 10); ++i) {
//         const auto& img = images[i];
//         // printf("%-12s | Temp: %6.0fK | RGB: (%.3f, %.3f, %.3f)\n",
//         //        img.name.c_str(),
//         //        img.temperature,
//         //        img.color.r, img.color.g, img.color.b);
//     }
//     // printf("... (%zu images processed)\n", images.size());
// }

int main(int argc, char** argv) {
    int num_images = 100000;
    int repeats = 2000;

    if (argc > 1) num_images = std::atoi(argv[1]);
    if (argc > 2) repeats = std::atoi(argv[2]);

    printf("Starting Filament Color Processor\n");
    printf("Processing %d simulated images (%d passes each)\n\n", num_images, repeats);

    auto images = generateImageSet(num_images);
    processImages(images, repeats);
    // exportResults(images);

    printf("\nDone. Images processed successfully.\n");
    return 0;
}
