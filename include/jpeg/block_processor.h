#pragma once
#include "image.h"

namespace jpeg {

// Копирует область 8x8 пикселей начиная с (block_row*8, block_col*8) для заданного
// канала в вещественный Block. Пиксели за границей изображения заменяются ближайшим краевым.
Block extract_block(const Image& img, int block_row, int block_col, int channel);

// Записывает вещественные значения Block обратно как uint8_t с зажимом в [0, 255].
void write_block(Image& img, int block_row, int block_col, int channel,
                 const Block& block);

// Число полных или неполных блоков 8x8, необходимых для покрытия измерения n.
inline int num_blocks(int n) noexcept { return (n + 7) / 8; }

// Таблица зигзаг-порядка (стандарт JPEG), используется для последовательного энтропийного сканирования.
// Элемент i содержит плоский индекс i-го коэффициента в зигзаг-порядке.
extern const int ZIGZAG_ORDER[64];

// Выполняет последовательный зигзаг-обход квантованного блока, имитируя
// зависимый от данных обход, который не позволяет тривиально распараллелить сканирование.
// Возвращает коэффициенты в зигзаг-порядке.
std::array<int16_t, 64> zigzag_scan(const Block& quantized_block);

} // namespace jpeg
