#include "jpeg/dct.h"
#include <cmath>
#include <stdexcept>

namespace jpeg {

DctPlan::DctPlan(int n) : n_(n) {
    if (n < 1 || n > kMaxBlockSize) {
        throw std::invalid_argument("DctPlan: размер блока вне диапазона [1, kMaxBlockSize]");
    }
    const double pi = std::acos(-1.0);
    forward_.resize(static_cast<size_t>(n) * n);
    inverse_.resize(static_cast<size_t>(n) * n);

    for (int k = 0; k < n; ++k) {
        // Ортонормальный масштаб: обеспечивает обратимость без дополнительных множителей.
        const double scale = (k == 0) ? 1.0 / std::sqrt(static_cast<double>(n))
                                      : std::sqrt(2.0 / n);
        for (int i = 0; i < n; ++i) {
            const double v = scale * std::cos(pi * (2 * i + 1) * k / (2.0 * n));
            forward_[static_cast<size_t>(k) * n + i] = v;
            inverse_[static_cast<size_t>(i) * n + k] = v;
        }
    }
}

// Одномерное прямое ДКП-II для n значений, расположенных в data с шагом step.
static void dct1d(const DctPlan& plan, float* data, int step) {
    const int n = plan.size();
    const double* basis = plan.forward();
    double tmp[kMaxBlockSize];

    for (int k = 0; k < n; ++k) {
        const double* row = basis + static_cast<size_t>(k) * n;
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += data[i * step] * row[i];
        tmp[k] = sum;
    }
    for (int k = 0; k < n; ++k) data[k * step] = static_cast<float>(tmp[k]);
}

// Одномерное обратное ДКП (ДКП-III) для n значений с шагом step.
static void idct1d(const DctPlan& plan, float* data, int step) {
    const int n = plan.size();
    const double* basis = plan.inverse();
    double tmp[kMaxBlockSize];

    for (int i = 0; i < n; ++i) {
        const double* row = basis + static_cast<size_t>(i) * n;
        double sum = 0.0;
        for (int k = 0; k < n; ++k) sum += data[k * step] * row[k];
        tmp[i] = sum;
    }
    for (int i = 0; i < n; ++i) data[i * step] = static_cast<float>(tmp[i]);
}

void dct2d(const DctPlan& plan, Block& block) {
    const int n = plan.size();

    // 1D ДКП по каждой строке (шаг = 1 внутри строки).
    for (int row = 0; row < n; ++row) dct1d(plan, &block[static_cast<size_t>(row) * n], 1);
    // 1D ДКП по каждому столбцу (шаг = n между строками).
    for (int col = 0; col < n; ++col) dct1d(plan, &block[col], n);
}

void idct2d(const DctPlan& plan, Block& block) {
    const int n = plan.size();

    // Обратное ДКП: сначала по столбцам, затем по строкам (обратный порядок).
    for (int col = 0; col < n; ++col) idct1d(plan, &block[col], n);
    for (int row = 0; row < n; ++row) idct1d(plan, &block[static_cast<size_t>(row) * n], 1);
}

} // namespace jpeg
