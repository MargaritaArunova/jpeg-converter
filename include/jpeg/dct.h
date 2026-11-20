#pragma once
#include "image.h"
#include <vector>

namespace jpeg {

// Максимальный поддерживаемый размер блока (размер буфера на стеке в 1D-преобразовании).
inline constexpr int kMaxBlockSize = 128;

// Предвычисленные базисные косинусы ортонормального ДКП-II размера N x N.
// Создаётся один раз и далее только читается, поэтому потоки делят её без блокировок.
class DctPlan {
public:
    explicit DctPlan(int n);

    int size() const noexcept { return n_; }

    // forward()[k*n + i] = c(k) * cos(pi * (2i+1) * k / (2n)); c(0) = 1/sqrt(n), иначе sqrt(2/n).
    const double* forward() const noexcept { return forward_.data(); }
    // Транспонированная матрица: inverse()[i*n + k] == forward()[k*n + i] (непрерывный доступ).
    const double* inverse() const noexcept { return inverse_.data(); }

private:
    int n_;
    std::vector<double> forward_;
    std::vector<double> inverse_;
};

// Прямое двумерное ДКП-II на месте для блока N x N.
// Сепарабельное: применяет 1D ДКП-II сначала к каждой строке, затем к каждому столбцу.
// Ортонормально масштабировано: idct2d(dct2d(x)) == x без дополнительных множителей.
void dct2d(const DctPlan& plan, Block& block);

// Обратное двумерное ДКП (ДКП-III) на месте — восстанавливает пространственные значения.
void idct2d(const DctPlan& plan, Block& block);

} // namespace jpeg
