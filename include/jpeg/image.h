#pragma once
#include <vector>
#include <array>
#include <cstdint>

namespace jpeg {

enum class ColorSpace { RGB, YCbCr };

// Плоское изображение в формате row-major с чередующимися каналами (R,G,B или Y,Cb,Cr на пиксель).
struct Image {
    int width;
    int height;
    int channels;
    std::vector<uint8_t> data;
    ColorSpace color_space;

    Image(int w, int h, int ch = 3, ColorSpace cs = ColorSpace::RGB)
        : width(w), height(h), channels(ch),
          data(static_cast<size_t>(w) * h * ch, 0),
          color_space(cs) {}

    uint8_t& at(int row, int col, int ch) noexcept {
        return data[(static_cast<size_t>(row) * width + col) * channels + ch];
    }
    uint8_t at(int row, int col, int ch) const noexcept {
        return data[(static_cast<size_t>(row) * width + col) * channels + ch];
    }

    // Общее число пикселей в изображении.
    int pixel_count() const noexcept { return width * height; }
};

// Блок N x N вещественных значений (row-major) — базовая единица JPEG-конвейера.
// Размер N выбирается при создании блока; в стандартном JPEG N = 8.
using Block = std::vector<float>;

} // namespace jpeg
