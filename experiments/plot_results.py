#!/usr/bin/env python3
"""Усредняет прогоны из experiments/raw/run_*/ и строит графики.

Выход:
    experiments/results/summary_<сценарий>.csv   -- среднее и std по прогонам для каждой точки
    experiments/plots/<сценарий>/time_grid.png    -- время от числа потоков, по панели на размер картинки
    experiments/plots/<сценарий>/speedup_grid.png -- ускорение T(1 поток)/T(n потоков)
    experiments/plots/<сценарий>/by_size/time_<размер>.png, speedup_<размер>.png -- отдельный график на размер картинки

Одна линия = один размер блока (оттенки одного цвета: тёмнее = больше блок).
Полоса вокруг линии = ±1 стандартное отклонение по прогонам.

Зависимости: pip install matplotlib numpy
"""

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FuncFormatter  # noqa: E402
import numpy as np  # noqa: E402

HERE = Path(__file__).resolve().parent

TITLES = {
    "dct_threads": "DCT-конвейер, std::thread (fork-join)",
    "dct_pool":    "DCT-конвейер, ThreadPool",
    "color":       "Преобразование RGB -> YCbCr",
    "batch":       "Пакет из 4 изображений через ThreadPool",
    "amdahl":      "Параллельный ДКП + последовательный зигзаг (закон Амдала)",
}

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
# Порядковая шкала одного оттенка (синий), светлый -> тёмный; проверена validate_palette.js --ordinal.
RAMP_6 = ["#86b6ef", "#6d9bd3", "#5480b8", "#3c679e", "#254e84", "#0d366b"]
SINGLE = "#2a78d6"


def ramp(n):
    if n == 1:
        return [SINGLE]
    if n == len(RAMP_6):
        return RAMP_6
    # Для другого числа блоков берём равномерные точки той же шкалы.
    idx = np.linspace(0, len(RAMP_6) - 1, n).round().astype(int)
    return [RAMP_6[i] for i in idx]


def load(raw_dir: Path):
    """-> {scenario: {(size, block, threads): [time_ms по прогонам]}}"""
    data = defaultdict(lambda: defaultdict(list))
    runs = sorted(raw_dir.glob("run_*"))
    if not runs:
        raise SystemExit(f"Нет прогонов в {raw_dir}. Сначала запустите run_experiments.py.")
    for run in runs:
        for path in sorted(run.glob("*.csv")):
            with path.open(newline="") as f:
                for row in csv.DictReader(f):
                    key = (int(row["image_size"]), int(row["block_size"]), int(row["threads"]))
                    data[row["scenario"]][key].append(float(row["time_ms"]))
    return data, len(runs)


def aggregate(points):
    return {k: (float(np.mean(v)), float(np.std(v)), len(v)) for k, v in points.items()}


def write_summary(scenario, agg, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    with (out_dir / f"summary_{scenario}.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["image_size", "block_size", "threads", "runs", "mean_ms", "std_ms"])
        for (size, block, threads), (mean, std, n) in sorted(agg.items()):
            w.writerow([size, block, threads, n, f"{mean:.6f}", f"{std:.6f}"])


def style_axes(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(AXIS)
    ax.tick_params(which="both", colors=MUTED, labelsize=8)
    ax.tick_params(which="major", length=3)
    ax.tick_params(which="minor", length=2)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def draw_panel(ax, agg, size, blocks, threads_all, metric):
    colors = ramp(len(blocks))
    for block, color in zip(blocks, colors):
        pts = sorted((t, agg[(size, block, t)]) for t in threads_all if (size, block, t) in agg)
        if not pts:
            continue
        xs = np.array([t for t, _ in pts], dtype=float)
        mean = np.array([v[0] for _, v in pts])
        std = np.array([v[1] for _, v in pts])
        if metric == "speedup":
            base = agg.get((size, block, 1))
            if base is None:
                continue
            y = base[0] / mean
            # Относительная ошибка отношения в первом приближении: складываем относительные std.
            rel = np.sqrt((std / mean) ** 2 + (base[1] / base[0]) ** 2)
            lo, hi = y * (1 - rel), y * (1 + rel)
        else:
            y, lo, hi = mean, np.maximum(mean - std, 1e-9), mean + std
        ax.fill_between(xs, lo, hi, color=color, alpha=0.10, linewidth=0)
        ax.plot(xs, y, color=color, linewidth=1.6, solid_capstyle="round", marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.5, zorder=3)

    if metric == "speedup":
        top = max(threads_all)
        ax.plot([1, top], [1, top], color=MUTED, linewidth=1.0, zorder=1)
        ax.set_ylim(0, None)
        ax.set_ylabel("Ускорение, раз", color=INK_2, fontsize=9)
    else:
        ax.set_yscale("log")
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
        ax.yaxis.set_minor_formatter(FuncFormatter(lambda v, _: f"{v:g}" if v / 10 ** math.floor(math.log10(v)) in (2, 5) else ""))
        ax.set_ylabel("Время, мс", color=INK_2, fontsize=9)

    ax.set_xticks(threads_all)
    ax.set_xlim(min(threads_all) - 0.5, max(threads_all) + 0.5)
    ax.set_xlabel("Потоки", color=INK_2, fontsize=9)
    style_axes(ax)


def legend_handles(blocks):
    from matplotlib.lines import Line2D

    out = []
    for block, color in zip(blocks, ramp(len(blocks))):
        label = f"блок {block}x{block}" if block else "все блоки"
        out.append(Line2D([0], [0], color=color, linewidth=1.6, marker="o", markersize=6,
                          markeredgecolor=SURFACE, markeredgewidth=1.5, label=label))
    return out


def plot_scenario(scenario, agg, plots_dir: Path, n_runs: int, individual: bool):
    sizes = sorted({k[0] for k in agg})
    blocks = sorted({k[1] for k in agg})
    threads_all = sorted({k[2] for k in agg})
    title = TITLES.get(scenario, scenario)
    out_dir = plots_dir / scenario
    out_dir.mkdir(parents=True, exist_ok=True)
    show_legend = len(blocks) > 1
    handles = legend_handles(blocks)

    for metric, name in (("time", "Время выполнения"), ("speedup", "Ускорение относительно 1 потока")):
        # Общая сетка: панель на каждый размер картинки.
        cols = min(3, len(sizes))
        rows = math.ceil(len(sizes) / cols)
        fig, axes = plt.subplots(rows, cols, figsize=(4.4 * cols, 3.3 * rows + 0.9),
                                 squeeze=False, facecolor=SURFACE)
        for ax in axes.flat:
            ax.set_visible(False)
        for ax, size in zip(axes.flat, sizes):
            ax.set_visible(True)
            draw_panel(ax, agg, size, blocks, threads_all, metric)
            ax.set_title(f"{size}x{size}", loc="left", color=INK, fontsize=10, fontweight="bold")
        fig.suptitle(f"{title}. {name}", x=0.01, ha="left", color=INK, fontsize=13, fontweight="bold")
        if show_legend:
            fig.legend(handles=handles, loc="upper left", bbox_to_anchor=(0.01, 0.955), ncol=len(blocks),
                       frameon=False, fontsize=9, labelcolor=INK_2, handlelength=1.6)
        fig.text(0.01, 0.005, f"Среднее по {n_runs} прогонам; полоса = ±1 std.", color=MUTED, fontsize=8)
        fig.tight_layout(rect=(0, 0.02, 1, 0.93 if show_legend else 0.96))
        fig.savefig(out_dir / f"{metric}_grid.png", dpi=150, facecolor=SURFACE)
        plt.close(fig)

        if not individual:
            continue
        by_size = out_dir / "by_size"
        by_size.mkdir(exist_ok=True)
        for size in sizes:
            fig, ax = plt.subplots(figsize=(6.4, 4.4), facecolor=SURFACE)
            draw_panel(ax, agg, size, blocks, threads_all, metric)
            ax.set_title(f"{title}. {name}, изображение {size}x{size}", loc="left", color=INK,
                         fontsize=10, fontweight="bold")
            if show_legend:
                ax.legend(handles=handles, loc="upper center", bbox_to_anchor=(0.5, -0.18), ncol=3,
                          frameon=False, fontsize=8, labelcolor=INK_2)
            fig.tight_layout()
            fig.savefig(by_size / f"{metric}_{size}.png", dpi=150, facecolor=SURFACE)
            plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", type=Path, default=HERE / "raw")
    ap.add_argument("--results", type=Path, default=HERE / "results")
    ap.add_argument("--plots", type=Path, default=HERE / "plots")
    ap.add_argument("--scenarios", default="", help="через запятую; по умолчанию все, что есть в raw")
    ap.add_argument("--no-individual", action="store_true", help="не строить отдельные графики на каждый размер")
    args = ap.parse_args()

    data, n_runs = load(args.raw)
    wanted = [s for s in args.scenarios.split(",") if s] or sorted(data)
    for scenario in wanted:
        if scenario == "sequential":
            agg = aggregate(data[scenario])
            write_summary(scenario, agg, args.results)
            continue
        if scenario not in data:
            print(f"Нет данных для {scenario}, пропуск")
            continue
        agg = aggregate(data[scenario])
        write_summary(scenario, agg, args.results)
        plot_scenario(scenario, agg, args.plots, n_runs, not args.no_individual)
        print(f"{scenario}: {len(agg)} точек, графики в {args.plots / scenario}")


if __name__ == "__main__":
    main()
