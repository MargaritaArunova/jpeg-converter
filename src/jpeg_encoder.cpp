#include "jpeg/jpeg_encoder.h"
#include "jpeg/color_converter.h"
#include "jpeg/dct.h"
#include "jpeg/quantization.h"
#include "jpeg/block_processor.h"
#include "jpeg/thread_pool.h"

#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <chrono>

namespace jpeg {

using Clock = std::chrono::high_resolution_clock;

JpegEncoder::JpegEncoder(int quality, int block_size)
    : quality_(quality),
      block_size_(block_size),
      luma_table_(scale_quant_table(LUMA_QUANT_TABLE, quality, block_size)),
      chroma_table_(scale_quant_table(CHROMA_QUANT_TABLE, quality, block_size)),
      dct_plan_(block_size),
      zigzag_order_(make_zigzag_order(block_size)) {}

// Запускает полный DCT-конвейер для блок-строк [row_start, row_end) по всем каналам.
// Является единицей параллелизма: разные диапазоны строк не пересекаются по данным.
void JpegEncoder::process_block_range(Image& img, int row_start, int row_end) {
    const int n     = block_size_;
    const int bcols = num_blocks(img.width, n);

    // Рабочий буфер у каждого вызова свой (локальный), поэтому потоки не делят изменяемых данных.
    Block block(static_cast<size_t>(n) * n);

    for (int br = row_start; br < row_end; ++br) {
        for (int bc = 0; bc < bcols; ++bc) {
            for (int ch = 0; ch < img.channels; ++ch) {
                const QuantTable& qt = (ch == 0) ? luma_table_ : chroma_table_;

                extract_block(img, br, bc, ch, n, block);
                dct2d(dct_plan_, block);
                quantize(block, qt);
                dequantize(block, qt);
                idct2d(dct_plan_, block);
                write_block(img, br, bc, ch, n, block);
            }
        }
    }
}

// --- Последовательный вариант ---

RunResult JpegEncoder::encode_sequential(Image& img) {
    auto t0 = Clock::now();

    rgb_to_ycbcr_sequential(img);
    process_block_range(img, 0, num_blocks(img.height, block_size_));
    ycbcr_to_rgb_sequential(img);

    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return {ms, 0, "Sequential"};
}

// --- Параллельный вариант: сырые std::thread (паттерн fork-join) ---
//
// Блок-строки делятся на num_threads непрерывных диапазонов.
// Каждый поток полностью независимо обрабатывает свой диапазон —
// потоки пишут в разные области img.data, мьютекс не нужен.
// std::atomic<int> считает завершённые задачи без блокировки (демонстрация атомарных операций).

RunResult JpegEncoder::encode_parallel_threads(Image& img, int num_threads) {
    const int brows = num_blocks(img.height, block_size_);
    num_threads = std::max(1, std::min(num_threads, brows));

    auto t0 = Clock::now();

    rgb_to_ycbcr_parallel(img, num_threads);

    // --- Fork: создаём потоки и раздаём работу ---
    int chunk = (brows + num_threads - 1) / num_threads;
    std::atomic<int> done_count{0};
    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        int row_start = t * chunk;
        int row_end   = std::min(row_start + chunk, brows);
        if (row_start >= brows) break;

        workers.emplace_back([this, &img, row_start, row_end, &done_count] {
            process_block_range(img, row_start, row_end);
            done_count.fetch_add(1, std::memory_order_relaxed); // атомарный инкремент без блокировки
        });
    }

    // --- Join: ждём завершения всех потоков ---
    for (auto& w : workers) w.join();

    ycbcr_to_rgb_parallel(img, num_threads);

    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return {ms, num_threads, "Parallel (std::thread fork-join)"};
}

// --- Параллельный вариант через ThreadPool ---
//
// Каждая блок-строка отправляется в пул как отдельная задача.
// std::future<void> собираются, чтобы вызывающий мог дождаться всех задач.

RunResult JpegEncoder::encode_with_pool(Image& img, int num_threads) {
    const int brows = num_blocks(img.height, block_size_);

    auto t0 = Clock::now();

    rgb_to_ycbcr_parallel(img, num_threads);

    {
        ThreadPool pool(num_threads);
        std::vector<std::future<void>> futures;
        futures.reserve(brows);

        for (int br = 0; br < brows; ++br) {
            futures.push_back(pool.submit(
                [this, &img, br] { process_block_range(img, br, br + 1); }
            ));
        }
        // Блокируемся до готовности каждого future — ждём завершения всех задач.
        for (auto& f : futures) f.get();
    }

    ycbcr_to_rgb_parallel(img, num_threads);

    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return {ms, num_threads, "Parallel (ThreadPool)"};
}

// --- Только преобразование цвета параллельно (изолированное измерение) ---

RunResult JpegEncoder::encode_parallel_color_only(Image& img, int num_threads) {
    const int brows = num_blocks(img.height, block_size_);

    auto t0 = Clock::now();

    rgb_to_ycbcr_parallel(img, num_threads); // исследуемый параллельный шаг
    process_block_range(img, 0, brows);       // ДКП выполняется последовательно (не измеряется отдельно)
    ycbcr_to_rgb_parallel(img, num_threads);

    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return {ms, num_threads, "Parallel color conversion only"};
}

// --- Демонстрация закона Амдала ---
//
// Блоки DCT обрабатываются параллельно (~80% общей работы), затем каждый блок
// проходит обязательное последовательное зигзаг-сканирование (~20% работы).
// Даже при бесконечном числе потоков закон Амдала ограничивает ускорение: 1/0.2 = 5x.

RunResult JpegEncoder::encode_amdahl_demo(Image& img, int num_threads) {
    const int brows = num_blocks(img.height, block_size_);
    const int bcols = num_blocks(img.width, block_size_);

    auto t0 = Clock::now();

    rgb_to_ycbcr_parallel(img, num_threads);

    // Параллельная часть: прямой ДКП + квантование + деквантование + обратный ДКП.
    {
        int chunk = (brows + num_threads - 1) / num_threads;
        std::vector<std::thread> workers;
        workers.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            int rs = t * chunk;
            int re = std::min(rs + chunk, brows);
            if (rs >= brows) break;
            workers.emplace_back([this, &img, rs, re] {
                process_block_range(img, rs, re);
            });
        }
        for (auto& w : workers) w.join();
    }

    // Последовательная часть: зигзаг-сканирование каждого восстановленного блока.
    // Нельзя распараллелить: в реальном кодировщике Хаффмана каждый символ зависит
    // от предыдущего (carry между блоками при арифметическом кодировании).
    {
        const int n = block_size_;
        Block blk(static_cast<size_t>(n) * n);
        std::vector<int16_t> scan(static_cast<size_t>(n) * n);
        for (int br = 0; br < brows; ++br) {
            for (int bc = 0; bc < bcols; ++bc) {
                for (int ch = 0; ch < img.channels; ++ch) {
                    extract_block(img, br, bc, ch, n, blk);
                    zigzag_scan(blk, zigzag_order_, scan);
                }
            }
        }
        // Результат сканирования намеренно не используется —
        // в реальном кодировщике он поступил бы на вход Хаффмана.
        volatile int16_t sink = scan[0];
        (void)sink;
    }

    ycbcr_to_rgb_parallel(img, num_threads);

    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return {ms, num_threads, "Parallel DCT + sequential zigzag (Amdahl demo)"};
}

} // namespace jpeg
