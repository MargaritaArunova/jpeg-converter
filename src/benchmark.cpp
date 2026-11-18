#include "jpeg/benchmark.h"
#include "jpeg/thread_pool.h"
#include "jpeg/color_converter.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <future>
#include <cmath>
#include <chrono>

namespace jpeg {

using Clock = std::chrono::high_resolution_clock;

// --- Генерация тестового изображения ---

Image generate_test_image(int width, int height) {
    Image img(width, height);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            // Плавный градиент смешан с диагональным паттерном, чтобы ДКП
            // порождал ненулевые коэффициенты на всех частотах (нетривиальная нагрузка).
            img.at(row, col, 0) = static_cast<uint8_t>((row * 255) / height);
            img.at(row, col, 1) = static_cast<uint8_t>((col * 255) / width);
            img.at(row, col, 2) = static_cast<uint8_t>(((row + col) % 256));
        }
    }
    return img;
}

// --- Форматированный вывод результатов ---

void print_results(const std::vector<RunResult>& results) {
    if (results.empty()) return;

    double seq_ms = results[0].elapsed_ms; // первый результат всегда — последовательная базовая линия

    std::cout << std::left
              << std::setw(44) << "Метод"
              << std::setw(10) << "Потоки"
              << std::setw(14) << "Время (мс)"
              << std::setw(10) << "Ускорение"
              << "\n";
    std::cout << std::string(76, '-') << "\n";

    for (const auto& r : results) {
        double speedup = seq_ms / r.elapsed_ms;
        std::cout << std::left
                  << std::setw(44) << r.method
                  << std::setw(10) << (r.num_threads == 0 ? 1 : r.num_threads)
                  << std::fixed << std::setprecision(2)
                  << std::setw(14) << r.elapsed_ms
                  << std::setw(10) << speedup
                  << "\n";
    }
    std::cout << "\n";
}

// --- Сценарий A: масштабирование DCT с числом потоков ---
//
// Суть метода сжатия: изображение делится на блоки 8x8 пикселей. Для каждого блока
// выполняется дискретное косинусное преобразование (ДКП), которое переводит
// пространственные значения яркости/цвета в набор частотных коэффициентов.
// Коэффициенты квантуются (делятся на табличные значения с округлением) —
// это единственный необратимый шаг, уменьшающий точность данных.
// Затем выполняется обратный ДКП для восстановления пикселей.
// Каждый блок 8x8 полностью независим от соседних, поэтому задача
// является «embarrassingly parallel» — идеальной для распараллеливания.

void bench_dct_scaling(int width, int height, int max_threads) {
    std::cout << "=== Сценарий A: DCT-сжатие (" << width << "x" << height
              << " пикселей) ===\n";
    std::cout << "Вычислительно интенсивная задача. Ожидается: почти линейное ускорение.\n\n";

    JpegEncoder enc(50);
    std::vector<RunResult> results;

    {
        Image img = generate_test_image(width, height);
        results.push_back(enc.encode_sequential(img));
    }
    for (int t = 2; t <= max_threads; t *= 2) {
        Image img = generate_test_image(width, height);
        results.push_back(enc.encode_parallel_threads(img, t));
    }
    for (int t = 2; t <= max_threads; t *= 2) {
        Image img = generate_test_image(width, height);
        results.push_back(enc.encode_with_pool(img, t));
    }

    print_results(results);
}

// --- Сценарий B: маленькое изображение — накладные расходы на потоки преобладают ---
//
// Суть демонстрации: тот же алгоритм сжатия (ДКП блоков), что и в сценарии A,
// но применённый к изображению 64x64. Таких блоков всего 64 штуки.
// Создание каждого потока ОС занимает ~50–200 мкс; реальная работа по сжатию
// 64 блоков занимает меньше. В итоге параллельная версия тратит больше времени
// на запуск и остановку потоков, чем на само вычисление.
// Вывод: параллелизм имеет смысл только при достаточно большом объёме работы.

void bench_small_image_overhead(int width, int height) {
    std::cout << "=== Сценарий B: маленькое изображение (" << width << "x" << height
              << " пикселей) ===\n";
    std::cout << "Создание потоков дороже самой работы. Ожидается: параллельно МЕДЛЕННЕЕ.\n\n";

    JpegEncoder enc(50);
    std::vector<RunResult> results;

    {
        Image img = generate_test_image(width, height);
        results.push_back(enc.encode_sequential(img));
    }
    for (int t : {2, 4, 8}) {
        Image img = generate_test_image(width, height);
        results.push_back(enc.encode_parallel_threads(img, t));
    }

    print_results(results);
}

// --- Сценарий C: преобразование цвета в изоляции (ограничено полосой памяти) ---
//
// Суть метода: перед ДКП JPEG переводит пиксели из RGB в YCbCr. Y — яркость,
// Cb и Cr — цветоразностные компоненты. Человеческий глаз хуже различает
// детали цвета, чем детали яркости, поэтому к Cb/Cr применяют более грубое
// квантование. Преобразование каждого пикселя — несколько умножений и сложений,
// никаких зависимостей между соседними пикселями нет.
// Однако задача ограничена скоростью чтения/записи памяти: при малом объёме
// вычислений на пиксель все потоки упираются в одну шину памяти, и ускорение
// перестаёт расти раньше, чем при вычислительно-интенсивном ДКП.

void bench_color_conversion(int width, int height, int max_threads) {
    std::cout << "=== Сценарий C: преобразование цветового пространства ("
              << width << "x" << height << " пикселей) ===\n";
    std::cout << "Задача ограничена полосой памяти. Ускорение достигает плато раньше максимума.\n\n";

    std::vector<RunResult> results;

    // Последовательная базовая линия.
    {
        Image img = generate_test_image(width, height);
        auto t0 = Clock::now();
        rgb_to_ycbcr_sequential(img);
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        results.push_back({ms, 0, "rgb_to_ycbcr последовательно"});
    }
    for (int t = 2; t <= max_threads; t *= 2) {
        Image img = generate_test_image(width, height);
        auto t0 = Clock::now();
        rgb_to_ycbcr_parallel(img, t);
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        results.push_back({ms, t, "rgb_to_ycbcr параллельно"});
    }

    print_results(results);
}

// --- Сценарий D: пакетная обработка через ThreadPool ---
//
// Суть метода: когда нужно сжать много независимых изображений (например, конвертация
// фотоальбома), каждое изображение можно отдать в отдельный поток пула.
// Изображения не делят данные между собой — нет общих переменных, нет мьютексов.
// ThreadPool амортизирует создание потоков: потоки создаются один раз при запуске пула
// и переиспользуются для всех задач. Это особенно выгодно при большом числе
// коротких задач: стоимость создания потока не платится для каждой задачи.
// Ожидаемый результат близок к сценарию A — почти линейное ускорение.

void bench_batch_processing(int image_width, int image_height,
                             int num_images, int num_threads) {
    std::cout << "=== Сценарий D: пакетная обработка (" << num_images
              << " изображений " << image_width << "x" << image_height << ") ===\n";
    std::cout << "Параллелизм на уровне задач через ThreadPool. Ожидается: почти линейное ускорение.\n\n";

    JpegEncoder enc(50);
    std::vector<RunResult> results;

    // Последовательно: сжимаем изображения одно за другим.
    {
        auto t0 = Clock::now();
        for (int i = 0; i < num_images; ++i) {
            Image img = generate_test_image(image_width, image_height);
            enc.encode_sequential(img);
        }
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        results.push_back({ms, 0, "Пакет последовательно"});
    }

    // Параллельно: каждое изображение — отдельная задача в пуле.
    {
        auto t0 = Clock::now();
        {
            ThreadPool pool(num_threads);
            std::vector<std::future<void>> futures;
            futures.reserve(num_images);

            for (int i = 0; i < num_images; ++i) {
                futures.push_back(pool.submit([&enc, image_width, image_height] {
                    Image img = generate_test_image(image_width, image_height);
                    enc.encode_sequential(img); // каждая задача выполняет полный последовательный энкод
                }));
            }
            for (auto& f : futures) f.get();
        }
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        results.push_back({ms, num_threads, "Пакет через ThreadPool"});
    }

    print_results(results);
}

// --- Сценарий E: закон Амдала ---
//
// Суть метода: после параллельного ДКП каждый блок должен пройти зигзаг-сканирование —
// обход 64 коэффициентов по диагонали (от низких частот к высоким). Результат
// поступает на вход энтропийного кодировщика (Хаффман), который кодирует символы
// последовательно: длина кода следующего символа зависит от всех предыдущих.
// Таким образом, 20% работы жёстко последовательны, и никакое число потоков
// не позволит преодолеть этот предел.
// Формула закона Амдала: S(N) = 1 / (p_seq + p_par/N)
//   где p_seq = 0.20 (последовательная доля), p_par = 0.80 (параллельная доля).

void bench_amdahl_law(int width, int height, int max_threads) {
    std::cout << "=== Сценарий E: закон Амдала (" << width << "x" << height
              << " пикселей) ===\n";
    std::cout << "Последовательное зигзаг-сканирование (~20% работы) ограничивает ускорение.\n";
    std::cout << "Теоретический максимум (Амдал): 1 / (0.20 + 0.80/N)\n\n";

    JpegEncoder enc(50);
    std::vector<RunResult> results;

    {
        Image img = generate_test_image(width, height);
        // Reuse sequential baseline from encode_sequential for fair comparison.
        results.push_back(enc.encode_sequential(img));
    }
    for (int t = 2; t <= max_threads; t *= 2) {
        Image img = generate_test_image(width, height);
        results.push_back(enc.encode_amdahl_demo(img, t));
    }

    // Выводим теоретический предел Амдала рядом с измеренными результатами.
    double seq_ms = results[0].elapsed_ms;
    std::cout << std::left
              << std::setw(44) << "Метод"
              << std::setw(10) << "Потоки"
              << std::setw(14) << "Время (мс)"
              << std::setw(14) << "Факт. уск."
              << std::setw(14) << "Предел Амдала"
              << "\n";
    std::cout << std::string(90, '-') << "\n";

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        double n       = (r.num_threads == 0) ? 1.0 : r.num_threads;
        double amdahl  = 1.0 / (0.20 + 0.80 / n);
        double measured = seq_ms / r.elapsed_ms;

        std::cout << std::left
                  << std::setw(44) << r.method
                  << std::setw(10) << static_cast<int>(n)
                  << std::fixed << std::setprecision(2)
                  << std::setw(14) << r.elapsed_ms
                  << std::setw(14) << measured
                  << std::setw(14) << amdahl
                  << "\n";
    }
    std::cout << "\n";
}

// --- Запуск всех сценариев ---

void run_all_benchmarks(int max_threads) {
    const int W_LARGE = 2048, H_LARGE = 2048;
    const int W_SMALL = 64,   H_SMALL = 64;

    std::cout << "Аппаратных потоков: " << std::thread::hardware_concurrency()
              << "\nЗапуск бенчмарков с числом потоков до " << max_threads << ".\n"
              << std::string(76, '=') << "\n\n";

    bench_dct_scaling(W_LARGE, H_LARGE, max_threads);
    bench_small_image_overhead(W_SMALL, H_SMALL);
    bench_color_conversion(W_LARGE, H_LARGE, max_threads);
    bench_batch_processing(512, 512, 16, max_threads);
    bench_amdahl_law(W_LARGE, H_LARGE, max_threads);
}

} // namespace jpeg
