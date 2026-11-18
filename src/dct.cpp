#include "jpeg/dct.h"
#include <cmath>

namespace jpeg {

static constexpr int N = 8;
static const double PI = std::acos(-1.0);

// Ортонормальный масштабный коэффициент: 1/sqrt(N) для k=0, sqrt(2/N) для остальных.
// Обеспечивает обратимость: IDCT(DCT(x)) == x без дополнительных множителей.
static inline double dct_scale(int k) {
    return (k == 0) ? 1.0 / std::sqrt(static_cast<double>(N))
                    : std::sqrt(2.0 / N);
}

// Одномерное прямое ДКП-II для 8 значений, расположенных в массиве data с шагом step.
// Преобразует пространственные отсчёты в частотные коэффициенты.
static void dct1d(float* data, int step) {
    double tmp[N];
    for (int k = 0; k < N; ++k) {
        double sum = 0.0;
        for (int n = 0; n < N; ++n) {
            sum += data[n * step] * std::cos(PI * (2 * n + 1) * k / (2.0 * N));
        }
        tmp[k] = dct_scale(k) * sum;
    }
    for (int k = 0; k < N; ++k) data[k * step] = static_cast<float>(tmp[k]);
}

// Одномерное обратное ДКП (ДКП-III) для 8 значений с шагом step.
// Восстанавливает пространственные значения из частотных коэффициентов.
static void idct1d(float* data, int step) {
    double tmp[N];
    for (int n = 0; n < N; ++n) {
        double sum = dct_scale(0) * data[0];
        for (int k = 1; k < N; ++k) {
            sum += dct_scale(k) * data[k * step]
                   * std::cos(PI * (2 * n + 1) * k / (2.0 * N));
        }
        tmp[n] = sum;
    }
    for (int n = 0; n < N; ++n) data[n * step] = static_cast<float>(tmp[n]);
}

Block dct2d(const Block& input) {
    Block out = input;

    // Применяем 1D ДКП к каждой строке блока (шаг = 1 внутри строки).
    for (int row = 0; row < N; ++row) {
        dct1d(&out[row * N], 1);
    }
    // Применяем 1D ДКП к каждому столбцу (шаг = N между строками).
    for (int col = 0; col < N; ++col) {
        dct1d(&out[col], N);
    }
    return out;
}

Block idct2d(const Block& dct_coeffs) {
    Block out = dct_coeffs;

    // Обратное ДКП: сначала по столбцам, затем по строкам (обратный порядок).
    for (int col = 0; col < N; ++col) {
        idct1d(&out[col], N);
    }
    for (int row = 0; row < N; ++row) {
        idct1d(&out[row * N], 1);
    }
    return out;
}

} // namespace jpeg
