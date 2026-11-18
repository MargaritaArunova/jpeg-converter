#pragma once
#include "image.h"
#include "jpeg_encoder.h"
#include <vector>
#include <string>

namespace jpeg {

// Генерирует синтетическое изображение с плавным градиентом и диагональным паттерном,
// чтобы ДКП порождал ненулевые коэффициенты на всём диапазоне частот.
Image generate_test_image(int width, int height);

// Выводит форматированную таблицу сравнения RunResult относительно последовательной базовой линии.
void print_results(const std::vector<RunResult>& results);

// --- Сценарии бенчмарков ---

// Сценарий A: ДКП-сжатие — большое изображение, варьируем число потоков.
// Показывает почти линейное ускорение для вычислительно интенсивной задачи.
void bench_dct_scaling(int width, int height, int max_threads);

// Сценарий B: маленькое изображение — накладные расходы на создание потоков преобладают.
// Параллельная версия заметно медленнее последовательной.
void bench_small_image_overhead(int width, int height);

// Сценарий C: преобразование цвета в изоляции — ограничено полосой памяти.
// Ускорение достигает плато раньше, чем при вычислительно интенсивной работе.
void bench_color_conversion(int width, int height, int max_threads);

// Сценарий D: пакетная обработка — независимые изображения параллельно через ThreadPool.
// Почти линейное ускорение, так как изображения не делят данные.
void bench_batch_processing(int image_width, int image_height,
                             int num_images, int num_threads);

// Сценарий E: закон Амдала — последовательный зигзаг (~20%) ограничивает ускорение.
// Демонстрирует теоретический и измеренный максимум.
void bench_amdahl_law(int width, int height, int max_threads);

// Запускает все сценарии и выводит результаты.
void run_all_benchmarks(int max_threads);

} // namespace jpeg
