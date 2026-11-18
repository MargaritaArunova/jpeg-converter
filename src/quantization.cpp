#include "jpeg/quantization.h"
#include <algorithm>
#include <cmath>

namespace jpeg {

// Стандартная таблица квантования яркости JPEG (Приложение K стандарта ISO 10918-1).
// Малые значения в левом верхнем углу (низкие частоты) сохраняют больше деталей яркости.
const QuantTable LUMA_QUANT_TABLE = {{
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
const QuantTable CHROMA_QUANT_TABLE = {{
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
}};

QuantTable scale_quant_table(const QuantTable& base, int quality) {
    quality = std::clamp(quality, 1, 100);
    // Формула масштабирования IJG (Independent JPEG Group):
    // quality < 50 — агрессивное увеличение шага (низкое качество, малый размер файла);
    // quality >= 50 — плавное уменьшение шага (высокое качество, большой размер файла).
    int scale = (quality < 50) ? (5000 / quality) : (200 - 2 * quality);

    QuantTable result;
    for (int i = 0; i < 64; ++i) {
        int val = (base[i] * scale + 50) / 100;
        result[i] = static_cast<uint8_t>(std::clamp(val, 1, 255));
    }
    return result;
}

Block quantize(const Block& dct_coeffs, const QuantTable& table) {
    Block out;
    for (int i = 0; i < 64; ++i) {
        // Деление с округлением — это единственный необратимый (lossy) шаг JPEG.
        // Чем больше делитель (шаг квантования), тем грубее округление и меньше файл.
        out[i] = std::round(dct_coeffs[i] / table[i]);
    }
    return out;
}

Block dequantize(const Block& quantized, const QuantTable& table) {
    Block out;
    for (int i = 0; i < 64; ++i) {
        out[i] = quantized[i] * table[i];
    }
    return out;
}

} // namespace jpeg
