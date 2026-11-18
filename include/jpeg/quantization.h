#pragma once
#include "image.h"

namespace jpeg {

using QuantTable = std::array<uint8_t, 64>;

// Стандартная таблица квантования яркости JPEG (базовый режим, качество 50).
extern const QuantTable LUMA_QUANT_TABLE;

// Стандартная таблица квантования цветности JPEG (базовый режим, качество 50).
extern const QuantTable CHROMA_QUANT_TABLE;

// Масштабирует базовую таблицу квантования под заданное качество (1-100).
// Выше качество -> меньше делители -> меньше потеря информации.
QuantTable scale_quant_table(const QuantTable& base, int quality);

// Делит каждый ДКП-коэффициент на соответствующий шаг квантования с округлением.
// Именно это округление является единственным необратимым (lossy) шагом JPEG.
Block quantize(const Block& dct_coeffs, const QuantTable& table);

// Умножает квантованные коэффициенты обратно на шаг (восстанавливает масштаб, но не точность).
Block dequantize(const Block& quantized, const QuantTable& table);

} // namespace jpeg
