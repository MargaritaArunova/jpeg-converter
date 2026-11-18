#pragma once
#include "image.h"

namespace jpeg {

// Прямое двумерное ДКП-II для блока 8x8.
// Сепарабельное: применяет 1D ДКП-II сначала к каждой строке, затем к каждому столбцу.
// Ортонормально масштабировано: IDCT(DCT(x)) == x без дополнительных множителей.
Block dct2d(const Block& input);

// Обратное двумерное ДКП (ДКП-III) — восстанавливает пространственные значения пикселей.
Block idct2d(const Block& dct_coeffs);

} // namespace jpeg
