// Раннер экспериментов: измеряет время одного сценария для всей сетки
// (размер изображения) x (размер блока) x (число потоков) и пишет строки в CSV.
//
// Один запуск процесса = один сценарий = один прогон. Повторы прогонов и паузы между ними
// организует run_experiments.py.

#include "jpeg/benchmark.h"
#include "jpeg/color_converter.h"
#include "jpeg/jpeg_encoder.h"
#include "jpeg/thread_pool.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Короткие замеры повторяются, пока суммарное измеренное время не достигнет порога:
// иначе для изображений 8x8 (единицы микросекунд) таймер измеряет в основном шум.
constexpr double kMinTotalMs = 50.0;
constexpr int    kMaxReps    = 20;

struct Config {
    std::string      scenario;
    std::string      out_path;
    std::vector<int> sizes   = {8, 64, 512, 1024, 2048};
    std::vector<int> blocks  = {4, 8, 16, 64};
    std::vector<int> threads = {1, 2, 4, 6, 8, 16};
    int              batch_images = 4;
};

struct Sample {
    double mean_ms;
    int    reps;
};

// Вызывает once() (возвращает измеренные мс; подготовка данных в него не входит),
// пока накопленное время меньше порога, и возвращает среднее.
Sample sample(const std::function<double()>& once) {
    double sum = once();
    int reps = 1;
    while (sum < kMinTotalMs && reps < kMaxReps) {
        sum += once();
        ++reps;
    }
    return {sum / reps, reps};
}

std::vector<int> parse_list(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(std::stoi(item));
    return out;
}

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Прогрев: заставляет CPU выйти из режима энергосбережения до первого замера.
void warm_up() {
    jpeg::JpegEncoder enc(50, 8);
    jpeg::Image base = jpeg::generate_test_image(512, 512);
    auto t0 = Clock::now();
    while (ms_since(t0) < 500.0) {
        jpeg::Image img = base;
        enc.encode_sequential(img);
    }
}

class CsvWriter {
public:
    explicit CsvWriter(const std::string& path) : file_(path) {
        if (!file_) {
            std::cerr << "Не удалось открыть файл: " << path << "\n";
            std::exit(2);
        }
        file_ << "scenario,image_size,block_size,threads,reps,time_ms\n";
    }

    void row(const std::string& scenario, int size, int block, int threads, const Sample& s) {
        file_ << scenario << ',' << size << ',' << block << ',' << threads << ','
              << s.reps << ',' << s.mean_ms << '\n';
        file_.flush(); // частичные результаты не теряются при прерывании
        std::fprintf(stderr, "[%s] size=%d block=%d threads=%d -> %.4f ms (reps=%d)\n",
                     scenario.c_str(), size, block, threads, s.mean_ms, s.reps);
    }

private:
    std::ofstream file_;
};

// --- Сценарии ---

// Базовая линия: все этапы в одном потоке. threads = 0.
void run_sequential(const Config& cfg, CsvWriter& csv) {
    for (int size : cfg.sizes) {
        const jpeg::Image base = jpeg::generate_test_image(size, size);
        for (int block : cfg.blocks) {
            jpeg::JpegEncoder enc(50, block);
            csv.row(cfg.scenario, size, block, 0, sample([&] {
                jpeg::Image img = base;
                return enc.encode_sequential(img).elapsed_ms;
            }));
        }
    }
}

// Сценарий A: fork-join на std::thread (блок-строки делятся между потоками).
void run_dct_threads(const Config& cfg, CsvWriter& csv) {
    for (int size : cfg.sizes) {
        const jpeg::Image base = jpeg::generate_test_image(size, size);
        for (int block : cfg.blocks) {
            jpeg::JpegEncoder enc(50, block);
            for (int t : cfg.threads) {
                csv.row(cfg.scenario, size, block, t, sample([&] {
                    jpeg::Image img = base;
                    return enc.encode_parallel_threads(img, t).elapsed_ms;
                }));
            }
        }
    }
}

// Сценарий A (вариант): те же блок-строки, но задачами в ThreadPool.
void run_dct_pool(const Config& cfg, CsvWriter& csv) {
    for (int size : cfg.sizes) {
        const jpeg::Image base = jpeg::generate_test_image(size, size);
        for (int block : cfg.blocks) {
            jpeg::JpegEncoder enc(50, block);
            for (int t : cfg.threads) {
                csv.row(cfg.scenario, size, block, t, sample([&] {
                    jpeg::Image img = base;
                    return enc.encode_with_pool(img, t).elapsed_ms;
                }));
            }
        }
    }
}

// Сценарий C: только RGB -> YCbCr. От размера блока не зависит, поэтому block_size = 0.
void run_color(const Config& cfg, CsvWriter& csv) {
    for (int size : cfg.sizes) {
        const jpeg::Image base = jpeg::generate_test_image(size, size);
        for (int t : cfg.threads) {
            csv.row(cfg.scenario, size, 0, t, sample([&] {
                jpeg::Image img = base;
                auto t0 = Clock::now();
                jpeg::rgb_to_ycbcr_parallel(img, t);
                return ms_since(t0);
            }));
        }
    }
}

// Сценарий D: batch_images независимых изображений, каждое кодируется последовательно
// отдельной задачей пула. Время включает создание и остановку пула.
void run_batch(const Config& cfg, CsvWriter& csv) {
    for (int size : cfg.sizes) {
        const jpeg::Image base = jpeg::generate_test_image(size, size);
        for (int block : cfg.blocks) {
            jpeg::JpegEncoder enc(50, block);
            for (int t : cfg.threads) {
                csv.row(cfg.scenario, size, block, t, sample([&] {
                    std::vector<jpeg::Image> images(cfg.batch_images, base);
                    auto t0 = Clock::now();
                    {
                        jpeg::ThreadPool pool(t);
                        std::vector<std::future<void>> futures;
                        futures.reserve(images.size());
                        for (auto& img : images) {
                            futures.push_back(pool.submit([&enc, &img] { enc.encode_sequential(img); }));
                        }
                        for (auto& f : futures) f.get();
                    }
                    return ms_since(t0);
                }));
            }
        }
    }
}

// Сценарий E: параллельный ДКП + обязательное последовательное зигзаг-сканирование.
void run_amdahl(const Config& cfg, CsvWriter& csv) {
    for (int size : cfg.sizes) {
        const jpeg::Image base = jpeg::generate_test_image(size, size);
        for (int block : cfg.blocks) {
            jpeg::JpegEncoder enc(50, block);
            for (int t : cfg.threads) {
                csv.row(cfg.scenario, size, block, t, sample([&] {
                    jpeg::Image img = base;
                    return enc.encode_amdahl_demo(img, t).elapsed_ms;
                }));
            }
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Нет значения для " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if      (arg == "--scenario")      cfg.scenario     = next();
        else if (arg == "--out")           cfg.out_path     = next();
        else if (arg == "--sizes")         cfg.sizes        = parse_list(next());
        else if (arg == "--blocks")        cfg.blocks       = parse_list(next());
        else if (arg == "--threads")       cfg.threads      = parse_list(next());
        else if (arg == "--batch-images")  cfg.batch_images = std::stoi(next());
        else {
            std::cerr << "Неизвестный аргумент: " << arg << "\n";
            return 2;
        }
    }

    const std::vector<std::pair<std::string, void (*)(const Config&, CsvWriter&)>> scenarios = {
        {"sequential",  run_sequential},
        {"dct_threads", run_dct_threads},
        {"dct_pool",    run_dct_pool},
        {"color",       run_color},
        {"batch",       run_batch},
        {"amdahl",      run_amdahl},
    };

    if (cfg.scenario.empty() || cfg.out_path.empty()) {
        std::cerr << "Использование: experiment_runner --scenario NAME --out FILE.csv "
                     "[--sizes a,b] [--blocks a,b] [--threads a,b] [--batch-images N]\nСценарии:";
        for (const auto& s : scenarios) std::cerr << ' ' << s.first;
        std::cerr << "\n";
        return 2;
    }

    for (const auto& [name, fn] : scenarios) {
        if (name != cfg.scenario) continue;
        warm_up();
        CsvWriter csv(cfg.out_path);
        fn(cfg, csv);
        return 0;
    }

    std::cerr << "Неизвестный сценарий: " << cfg.scenario << "\n";
    return 2;
}
