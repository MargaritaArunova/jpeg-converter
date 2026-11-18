#include "jpeg/color_converter.h"
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>

namespace jpeg {

// Преобразует один пиксель из RGB в YCbCr по стандарту ITU-R BT.601.
// Y  — яркость (luma), Cb/Cr — цветоразностные компоненты (chroma).
// JPEG обрабатывает яркость и цветность раздельно: к Y применяется точная таблица
// квантования, к Cb/Cr — грубее, так как глаз менее чувствителен к цвету.
// Сдвиг на 128 переносит диапазон Cb/Cr с [0,255] к [-128,127] (стандарт JFIF).
static void convert_pixel_to_ycbcr(uint8_t* p) {
    const float r = p[0], g = p[1], b = p[2];
    p[0] = static_cast<uint8_t>(std::clamp( 0.299f*r + 0.587f*g + 0.114f*b,              0.f, 255.f));
    p[1] = static_cast<uint8_t>(std::clamp(-0.168736f*r - 0.331264f*g + 0.5f*b + 128.f,  0.f, 255.f));
    p[2] = static_cast<uint8_t>(std::clamp( 0.5f*r - 0.418688f*g - 0.081312f*b + 128.f,  0.f, 255.f));
}

// Обратное преобразование YCbCr -> RGB (применяется после декодирования).
static void convert_pixel_to_rgb(uint8_t* p) {
    const float y  = p[0], cb = p[1] - 128.f, cr = p[2] - 128.f;
    p[0] = static_cast<uint8_t>(std::clamp(y                + 1.402f*cr,              0.f, 255.f));
    p[1] = static_cast<uint8_t>(std::clamp(y - 0.344136f*cb - 0.714136f*cr,           0.f, 255.f));
    p[2] = static_cast<uint8_t>(std::clamp(y + 1.772f*cb,                             0.f, 255.f));
}

// Применяет pixel_fn к каждому пикселю строк [row_start, row_end).
static void convert_row_range(Image& img, int row_start, int row_end,
                               void (*pixel_fn)(uint8_t*)) {
    for (int row = row_start; row < row_end; ++row) {
        for (int col = 0; col < img.width; ++col) {
            pixel_fn(&img.data[(static_cast<size_t>(row) * img.width + col) * img.channels]);
        }
    }
}

// --- Последовательные реализации ---

void rgb_to_ycbcr_sequential(Image& img) {
    convert_row_range(img, 0, img.height, convert_pixel_to_ycbcr);
    img.color_space = ColorSpace::YCbCr;
}

void ycbcr_to_rgb_sequential(Image& img) {
    convert_row_range(img, 0, img.height, convert_pixel_to_rgb);
    img.color_space = ColorSpace::RGB;
}

// --- Параллельные реализации (паттерн fork-join с std::thread) ---
//
// Строки изображения делятся на num_threads непрерывных диапазонов.
// Каждый поток независимо обрабатывает свой диапазон — общих изменяемых данных нет,
// поэтому синхронизация внутри рабочих потоков не нужна.
// Главный поток ждёт завершения всех рабочих через join().

static void parallel_convert(Image& img, int num_threads, void (*pixel_fn)(uint8_t*),
                              ColorSpace result_cs) {
    num_threads = std::max(1, std::min(num_threads, img.height));
    int chunk = (img.height + num_threads - 1) / num_threads;

    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        int row_start = t * chunk;
        int row_end   = std::min(row_start + chunk, img.height);
        if (row_start >= img.height) break;
        workers.emplace_back(convert_row_range, std::ref(img),
                             row_start, row_end, pixel_fn);
    }

    for (auto& w : workers) w.join();
    img.color_space = result_cs;
}

void rgb_to_ycbcr_parallel(Image& img, int num_threads) {
    parallel_convert(img, num_threads, convert_pixel_to_ycbcr, ColorSpace::YCbCr);
}

void ycbcr_to_rgb_parallel(Image& img, int num_threads) {
    parallel_convert(img, num_threads, convert_pixel_to_rgb, ColorSpace::RGB);
}

} // namespace jpeg
