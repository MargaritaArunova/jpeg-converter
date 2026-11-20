#pragma once
#include "image.h"
#include "dct.h"
#include "quantization.h"
#include <chrono>
#include <string>
#include <vector>

namespace jpeg {

// Время выполнения и метаданные одного запуска энкодера.
struct RunResult {
    double      elapsed_ms;  // реальное время (wall-clock)
    int         num_threads; // 0 = последовательное выполнение
    std::string method;      // название метода для вывода
};

// Реализует конвейер JPEG-преобразования в нескольких вариантах для прямого
// сравнения последовательного и параллельного исполнения.
//
// Этапы конвейера:
//   1. RGB -> YCbCr (на каждый пиксель, embarrassingly parallel)
//   2. Извлечение блоков N x N (блоки независимы)
//   3. Прямой ДКП (независим на блок, вычислительно тяжёлый)
//   4. Квантование (независимо на блок)
//   5. Деквантование + обратный ДКП (независимо на блок)
//   6. Запись блоков обратно в изображение (независимо на блок)
//
// Этап 7 (зигзаг-сканирование / энтропийное кодирование) вынесен отдельно
// для демонстрации последовательного узкого места (закон Амдала).
//
// Размер блока N задаётся в конструкторе (по умолчанию 8, как в стандарте JPEG).
class JpegEncoder {
public:
    explicit JpegEncoder(int quality = 50, int block_size = 8);

    int block_size() const noexcept { return block_size_; }

    // Все этапы выполняются последовательно в вызывающем потоке.
    RunResult encode_sequential(Image& img);

    // Этапы 2-6 распределяются по сырым std::thread (паттерн fork-join).
    // Число потоков ограничивается числом блок-строк изображения.
    RunResult encode_parallel_threads(Image& img, int num_threads);

    // Этапы 2-6 отправляются в ThreadPool (паттерн producer-consumer).
    RunResult encode_with_pool(Image& img, int num_threads);

    // Параллелизуется только преобразование цвета — для изолированного измерения.
    RunResult encode_parallel_color_only(Image& img, int num_threads);

    // Параллельный ДКП с обязательным последовательным зигзаг-сканированием —
    // демонстрирует закон Амдала при последовательной доле ~20%.
    RunResult encode_amdahl_demo(Image& img, int num_threads);

private:
    int        quality_;
    int        block_size_;
    QuantTable luma_table_;
    QuantTable chroma_table_;
    DctPlan    dct_plan_;               // базисные косинусы, только чтение — безопасно делить между потоками
    std::vector<int> zigzag_order_;

    // Выполняет прямой ДКП, квантование, деквантование и обратный ДКП
    // для блок-строк в диапазоне [row_start, row_end).
    void process_block_range(Image& img, int row_start, int row_end);
};

} // namespace jpeg
