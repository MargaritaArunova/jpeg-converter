#pragma once
#include "image.h"
#include <array>
#include <vector>

namespace jpeg {

// Базовая таблица квантования 8x8 из стандарта JPEG.
using BaseQuantTable = std::array<uint8_t, 64>;

// Таблица квантования для блока N x N (row-major).
using QuantTable = std::vector<uint8_t>;

// Стандартная таблица квантования яркости JPEG (базовый режим, качество 50).
extern const BaseQuantTable LUMA_QUANT_TABLE;

// Стандартная таблица квантования цветности JPEG (базовый режим, качество 50).
extern const BaseQuantTable CHROMA_QUANT_TABLE;

// Масштабирует базовую таблицу под заданное качество (1-100) и размер блока.
// Выше качество -> меньше делители -> меньше потеря информации.
// Для block_size != 8 таблица 8x8 растягивается/прореживается так, чтобы одна и та же
// относительная частота получала тот же шаг квантования (block_size = 8 даёт исходную таблицу).
QuantTable scale_quant_table(const BaseQuantTable& base, int quality, int block_size = 8);

// Делит каждый ДКП-коэффициент на соответствующий шаг квантования с округлением (на месте).
// Именно это округление является единственным необратимым (lossy) шагом JPEG.
void quantize(Block& coeffs, const QuantTable& table);

// Умножает квантованные коэффициенты обратно на шаг (на месте): масштаб восстанавливается, точность — нет.
void dequantize(Block& coeffs, const QuantTable& table);

} // namespace jpeg
