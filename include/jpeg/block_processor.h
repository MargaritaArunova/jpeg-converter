#pragma once
#include "image.h"
#include <vector>

namespace jpeg {

// Копирует область n x n пикселей начиная с (block_row*n, block_col*n) для заданного
// канала в вещественный Block (размер out должен быть n*n). Пиксели за границей
// изображения заменяются ближайшим краевым.
void extract_block(const Image& img, int block_row, int block_col, int channel,
                   int n, Block& out);

// Записывает вещественные значения Block обратно как uint8_t с зажимом в [0, 255].
void write_block(Image& img, int block_row, int block_col, int channel,
                 int n, const Block& block);

// Число полных или неполных блоков n x n, необходимых для покрытия измерения dim.
inline int num_blocks(int dim, int n) noexcept { return (dim + n - 1) / n; }

// Строит зигзаг-порядок (стандарт JPEG) для блока n x n.
// Элемент r*n+c содержит номер позиции (r,c) при обходе по диагоналям от DC-коэффициента.
std::vector<int> make_zigzag_order(int n);

// Выполняет последовательный зигзаг-обход блока, имитируя зависимый от данных обход,
// который не позволяет тривиально распараллелить сканирование. out должен иметь размер n*n.
void zigzag_scan(const Block& block, const std::vector<int>& order,
                 std::vector<int16_t>& out);

} // namespace jpeg
