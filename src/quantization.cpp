#include "jpeg/quantization.h"
#include <algorithm>
#include <cmath>

namespace jpeg {

// Стандартная таблица квантования яркости JPEG (Приложение K стандарта ISO 10918-1).
// Малые значения в левом верхнем углу (низкие частоты) сохраняют больше деталей яркости.
const BaseQuantTable LUMA_QUANT_TABLE = {{
    16, 11, 10, 16,  24,  40,  51,  61,
    12, 12, 14, 19,  26,  58,  60,  55,
    14, 13, 16, 24,  40,  57,  69,  56,
    14, 17, 22, 29,  51,  87,  80,  62,
    18, 22, 37, 56,  68, 109, 103,  77,
    24, 35, 55, 64,  81, 104, 113,  92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103,  99
}};

// Стандартная таблица квантования цветности JPEG.
// Большие значения (99) в правом нижнем углу сильно подавляют высокие частоты цвета —
// глаз менее чувствителен к цветовым деталям, чем к деталям яркости.
const BaseQuantTable CHROMA_QUANT_TABLE = {{
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
}};

QuantTable scale_quant_table(const BaseQuantTable& base, int quality, int block_size) {
    quality = std::clamp(quality, 1, 100);
    // Формула масштабирования IJG (Independent JPEG Group):
    // quality < 50 — агрессивное увеличение шага (низкое качество, малый размер файла);
    // quality >= 50 — плавное уменьшение шага (высокое качество, большой размер файла).
    const int scale = (quality < 50) ? (5000 / quality) : (200 - 2 * quality);

    QuantTable result(static_cast<size_t>(block_size) * block_size);
    for (int r = 0; r < block_size; ++r) {
        for (int c = 0; c < block_size; ++c) {
            // Позиция (r,c) блока n x n соответствует позиции (r*8/n, c*8/n) базовой таблицы.
            const int src = (r * 8 / block_size) * 8 + (c * 8 / block_size);
            const int val = (base[src] * scale + 50) / 100;
            result[static_cast<size_t>(r) * block_size + c] = static_cast<uint8_t>(std::clamp(val, 1, 255));
        }
    }
    return result;
}

void quantize(Block& coeffs, const QuantTable& table) {
    // Деление с округлением — это единственный необратимый (lossy) шаг JPEG.
    // Чем больше делитель (шаг квантования), тем грубее округление и меньше файл.
    for (size_t i = 0; i < coeffs.size(); ++i) {
        coeffs[i] = std::round(coeffs[i] / table[i]);
    }
}

void dequantize(Block& coeffs, const QuantTable& table) {
    for (size_t i = 0; i < coeffs.size(); ++i) {
        coeffs[i] *= table[i];
    }
}

} // namespace jpeg
