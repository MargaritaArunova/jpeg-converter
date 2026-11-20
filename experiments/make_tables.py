#!/usr/bin/env python3
"""Строит сравнительные таблицы из experiments/results/summary_*.csv -> experiments/results/comparison.md.

Ячейка "время (ускорение)": среднее время в мс по прогонам и T(1 поток)/T(n потоков).
Звёздочка у значения = разброс по прогонам std/mean > 15% (точка шумная).
Зависимости: только стандартная библиотека.
"""

import argparse
import csv
from pathlib import Path

HERE = Path(__file__).resolve().parent
NOISY = 0.15
BATCH_IMAGES = 4  # значение по умолчанию experiment_runner; run_experiments.py его не меняет

NAMES = {
    "dct_threads": "DCT-конвейер, std::thread (fork-join)",
    "dct_pool":    "DCT-конвейер, ThreadPool",
    "amdahl":      "ДКП + последовательный зигзаг (Амдал)",
    "batch":       f"Пакет из {BATCH_IMAGES} изображений, ThreadPool",
}


def load(results: Path, scenario: str):
    path = results / f"summary_{scenario}.csv"
    if not path.exists():
        return {}
    with path.open(newline="") as f:
        return {(int(r["image_size"]), int(r["block_size"]), int(r["threads"])):
                (float(r["mean_ms"]), float(r["std_ms"])) for r in csv.DictReader(f)}


def fmt_ms(v: float) -> str:
    return f"{v:.3f}" if v < 10 else f"{v:.1f}" if v < 1000 else f"{v:.0f}"


def cell(d, key, base):
    if key not in d:
        return "—"
    mean, std = d[key]
    star = "*" if mean > 0 and std / mean > NOISY else ""
    return f"{fmt_ms(mean)}{star} ({base / mean:.2f}×)"


def table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "|".join("---" for _ in header) + "|"]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--results", type=Path, default=HERE / "results")
    ap.add_argument("--out", type=Path, default=HERE / "results" / "comparison.md")
    args = ap.parse_args()

    data = {s: load(args.results, s) for s in [*NAMES, "color", "sequential"]}
    L = ["# Сравнительные таблицы экспериментов", "",
         "Время — среднее по прогонам, мс. В скобках ускорение T(1 поток) / T(n потоков). "
         "`*` — разброс по прогонам больше 15% (std/mean), такие точки лучше не трактовать буквально.", ""]

    # 1. Сводка: ускорение на 8 и 16 потоках, сценарии рядом.
    scen = [s for s in NAMES if data[s]]
    L += ["## 1. Сводка: ускорение при 8 потоках (на 16 — в скобках)", ""]
    header = ["Картинка", "Блок"] + [NAMES[s] for s in scen]
    rows = []
    keys = sorted({(k[0], k[1]) for s in scen for k in data[s]})
    for size, block in keys:
        r = [f"{size}x{size}", f"{block}x{block}"]
        for s in scen:
            d = data[s]
            if (size, block, 1) in d and (size, block, 8) in d:
                t1 = d[(size, block, 1)][0]
                s8 = t1 / d[(size, block, 8)][0]
                s16 = t1 / d[(size, block, 16)][0] if (size, block, 16) in d else float("nan")
                r.append(f"{s8:.2f}× ({s16:.2f}×)")
            else:
                r.append("—")
        rows.append(r)
    L += table(header, rows) + [""]

    # 2. Накладные расходы: 1 поток против последовательной версии.
    seq = data["sequential"]
    if seq:
        L += ["## 2. Последовательная версия и накладные расходы на 1 потоке", "",
              "Отношение T(1 поток)/T(последовательно): 1.00 — параллельный код при одном потоке "
              "не медленнее последовательного, больше — накладные расходы на создание потоков/пула. "
              f"Для пакета делим ещё на {BATCH_IMAGES} (столько изображений кодируется).", ""]
        header = ["Картинка", "Блок", "Последовательно, мс"] + [f"{NAMES[s]}, 1 поток" for s in scen]
        rows = []
        for size, block in keys:
            if (size, block, 0) not in seq:
                continue
            t0 = seq[(size, block, 0)][0]
            r = [f"{size}x{size}", f"{block}x{block}", fmt_ms(t0)]
            for s in scen:
                d = data[s]
                work = BATCH_IMAGES if s == "batch" else 1  # в batch последовательной работы в N раз больше
                r.append(f"{d[(size, block, 1)][0] / (t0 * work):.2f}×" if (size, block, 1) in d else "—")
            rows.append(r)
        L += table(header, rows) + [""]

    # 3. Подробные таблицы: сценарий -> размер картинки -> блок x потоки.
    L += ["## 3. Подробно по сценариям", ""]
    for s in scen:
        d = data[s]
        threads = sorted({k[2] for k in d})
        L += [f"### {NAMES[s]}", ""]
        for size in sorted({k[0] for k in d}):
            L += [f"**{size}x{size}**", ""]
            rows = []
            for block in sorted({k[1] for k in d if k[0] == size}):
                base = d[(size, block, 1)][0]
                rows.append([f"{block}x{block}"] + [cell(d, (size, block, t), base) for t in threads])
            L += table(["Блок"] + [f"{t} пот." for t in threads], rows) + [""]

    # 4. Цветовое преобразование (без блоков).
    color = data["color"]
    if color:
        threads = sorted({k[2] for k in color})
        L += ["### Преобразование RGB → YCbCr (от размера блока не зависит)", ""]
        rows = []
        for size in sorted({k[0] for k in color}):
            base = color[(size, 0, 1)][0]
            rows.append([f"{size}x{size}"] + [cell(color, (size, 0, t), base) for t in threads])
        L += table(["Картинка"] + [f"{t} пот." for t in threads], rows) + [""]

    args.out.write_text("\n".join(L), encoding="utf-8")
    print(f"Записано: {args.out}")


if __name__ == "__main__":
    main()
