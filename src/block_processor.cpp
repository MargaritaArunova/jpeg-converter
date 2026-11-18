#include "jpeg/block_processor.h"
#include <algorithm>

namespace jpeg {

// Стандартный порядок зигзаг-обхода блока 8x8 (JPEG, ISO 10918-1).
// Обход начинается с DC-коэффициента (верхний левый угол) и идёт по диагоналям,
// группируя низкочастотные коэффициенты в начале массива. Это повышает эффективность
// последующего RLE-кодирования: длинные серии нулей чаще встречаются в конце.
const int ZIGZAG_ORDER[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

Block extract_block(const Image& img, int block_row, int block_col, int channel) {
    Block block;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            // Пограничные блоки: пиксели за краем изображения заменяются ближайшим краевым пикселем.
            int row = std::min(block_row * 8 + r, img.height - 1);
            int col = std::min(block_col * 8 + c, img.width  - 1);
            // Уровневый сдвиг на -128: центрирует значения вокруг нуля перед ДКП (стандарт JPEG).
            block[r * 8 + c] = static_cast<float>(img.at(row, col, channel)) - 128.f;
        }
    }
    return block;
}

void write_block(Image& img, int block_row, int block_col, int channel,
                 const Block& block) {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int row = block_row * 8 + r;
            int col = block_col * 8 + c;
            if (row >= img.height || col >= img.width) continue;
            // Обратный уровневый сдвиг (+128) и зажим в допустимый диапазон uint8_t.
            float val = block[r * 8 + c] + 128.f;
            img.at(row, col, channel) =
                static_cast<uint8_t>(std::clamp(std::round(val), 0.f, 255.f));
        }
    }
}

std::array<int16_t, 64> zigzag_scan(const Block& quantized_block) {
    std::array<int16_t, 64> out;
    for (int i = 0; i < 64; ++i) {
        out[ZIGZAG_ORDER[i]] = static_cast<int16_t>(quantized_block[i]);
    }
    return out;
}

} // namespace jpeg
