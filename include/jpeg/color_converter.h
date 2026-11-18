#pragma once
#include "image.h"

namespace jpeg {

// Преобразование RGB -> YCbCr на месте по коэффициентам ITU-R BT.601.
// Каждый пиксель полностью независим — шаг является embarrassingly parallel.
void rgb_to_ycbcr_sequential(Image& img);

// Делит строки на num_threads равных диапазонов, каждый обрабатывается отдельным std::thread.
void rgb_to_ycbcr_parallel(Image& img, int num_threads);

// Обратное преобразование YCbCr -> RGB (используется для проверки корректности round-trip).
void ycbcr_to_rgb_sequential(Image& img);
void ycbcr_to_rgb_parallel(Image& img, int num_threads);

} // namespace jpeg
