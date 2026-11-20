#include "jpeg/block_processor.h"
#include <algorithm>
#include <cmath>

namespace jpeg {

std::vector<int> make_zigzag_order(int n) {
    // Обход по диагоналям r+c = s; направление чередуется: чётные диагонали идут
    // снизу вверх, нечётные — сверху вниз. Для n = 8 даёт таблицу из стандарта ISO 10918-1.
    std::vector<int> order(static_cast<size_t>(n) * n);
    int index = 0;
    for (int s = 0; s <= 2 * n - 2; ++s) {
        const int r_min = std::max(0, s - n + 1);
        const int r_max = std::min(s, n - 1);
        if (s % 2 == 0) {
            for (int r = r_max; r >= r_min; --r) order[static_cast<size_t>(r) * n + (s - r)] = index++;
        } else {
            for (int r = r_min; r <= r_max; ++r) order[static_cast<size_t>(r) * n + (s - r)] = index++;
        }
    }
    return order;
}

void extract_block(const Image& img, int block_row, int block_col, int channel,
                   int n, Block& out) {
    for (int r = 0; r < n; ++r) {
        // Пограничные блоки: пиксели за краем изображения заменяются ближайшим краевым пикселем.
        const int row = std::min(block_row * n + r, img.height - 1);
        for (int c = 0; c < n; ++c) {
            const int col = std::min(block_col * n + c, img.width - 1);
            // Уровневый сдвиг на -128: центрирует значения вокруг нуля перед ДКП (стандарт JPEG).
            out[static_cast<size_t>(r) * n + c] = static_cast<float>(img.at(row, col, channel)) - 128.f;
        }
    }
}

void write_block(Image& img, int block_row, int block_col, int channel,
                 int n, const Block& block) {
    for (int r = 0; r < n; ++r) {
        const int row = block_row * n + r;
        if (row >= img.height) break;
        for (int c = 0; c < n; ++c) {
            const int col = block_col * n + c;
            if (col >= img.width) break;
            // Обратный уровневый сдвиг (+128) и зажим в допустимый диапазон uint8_t.
            const float val = block[static_cast<size_t>(r) * n + c] + 128.f;
            img.at(row, col, channel) =
                static_cast<uint8_t>(std::clamp(std::round(val), 0.f, 255.f));
        }
    }
}

void zigzag_scan(const Block& block, const std::vector<int>& order,
                 std::vector<int16_t>& out) {
    for (size_t i = 0; i < block.size(); ++i) {
        out[order[i]] = static_cast<int16_t>(block[i]);
    }
}

} // namespace jpeg
