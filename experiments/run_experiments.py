#!/usr/bin/env python3
"""Запускает серию экспериментов и сохраняет сырые результаты в experiments/raw/.

Структура результатов:
    experiments/raw/run_1/<сценарий>.csv
    ...
    experiments/raw/run_5/<сценарий>.csv

Каждый CSV: scenario,image_size,block_size,threads,reps,time_ms
(time_ms — среднее по reps внутренним повторам одного замера).

Прогоны выполняются строго последовательно, между прогонами и между сценариями
делается пауза, чтобы процессор успел остыть и ничто не влияло на следующий замер.

Примеры:
    python3 experiments/run_experiments.py --build-dir cmake-build-release
    python3 experiments/run_experiments.py --build-dir cmake-build-release --preset fast
    python3 experiments/run_experiments.py --build-dir cmake-build-release --quick
    python3 experiments/run_experiments.py --build-dir cmake-build-release --runs 5 --resume
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

SCENARIOS = {
    "sequential":  "последовательная базовая линия (все этапы в одном потоке)",
    "dct_threads": "сценарий A: DCT-конвейер, std::thread fork-join",
    "dct_pool":    "сценарий A: DCT-конвейер, ThreadPool",
    "color":       "сценарий C: только RGB -> YCbCr",
    "batch":       "сценарий D: пакет изображений через ThreadPool",
    "amdahl":      "сценарий E: параллельный ДКП + последовательный зигзаг",
}

# FULL = dict(
#     sizes="8,16,32,64,128,256,512,1024,2048",
#     blocks="4,8,16,32,64,128",
#     threads="1,2,3,4,5,6,7,8,16",
# )
FULL = dict(
    sizes="8, 64, 512, 1024, 2048",
    blocks="4, 8, 16, 64",
    threads="2, 4, 6, 8, 16",
)
QUICK = dict(sizes="8, 64, 512, 1024, 2048", blocks="4, 8, 16, 64", threads="1, 2, 4, 6, 8, 16")

# Пресет на ~15 минут для всей серии из 5 прогонов. batch на 2048x2048 исключён:
# 4 больших изображения на 1 потоке занимают минуты и дают большую часть времени серии.
FAST = dict(sizes="8,64,512,2048", blocks="8,16,64", threads="1,2,3,4,5,6,7,8,16",
            pause_runs=90, pause_scenarios=3,
            scenario_sizes={"batch": "8,64,512"})


def find_runner(build_dir: Path) -> Path:
    for candidate in (build_dir / "experiment_runner", build_dir / "Release" / "experiment_runner.exe"):
        if candidate.exists():
            return candidate
    sys.exit(f"experiment_runner не найден в {build_dir}. Сначала соберите проект в Release "
             f"(см. README: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build).")


def countdown(seconds: int, label: str) -> None:
    if seconds <= 0:
        return
    print(f"  пауза {seconds} с ({label})...", flush=True)
    time.sleep(seconds)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", type=Path, default=Path("cmake-build-release"),
                    help="папка сборки, где лежит experiment_runner")
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parent / "raw",
                    help="куда писать сырые результаты")
    ap.add_argument("--runs", type=int, default=3, help="число независимых прогонов")
    ap.add_argument("--preset", choices=["full", "fast"], default="full",
                    help="fast: ~15 минут на 5 прогонов (4 размера картинки, блоки 8/16/64, короткие паузы)")
    ap.add_argument("--pause-runs", type=int, help="пауза между прогонами, с (full: 120, fast: 90)")
    ap.add_argument("--pause-scenarios", type=int, help="пауза между сценариями, с (full: 15, fast: 3)")
    ap.add_argument("--scenarios", default=",".join(SCENARIOS), help="через запятую")
    ap.add_argument("--quick", action="store_true", help="уменьшенная сетка для проверки пайплайна")
    ap.add_argument("--resume", action="store_true", help="пропускать уже готовые CSV (продолжить после прерывания)")
    ap.add_argument("--sizes"); ap.add_argument("--blocks"); ap.add_argument("--threads")
    args = ap.parse_args()

    preset = FAST if args.preset == "fast" else {}
    grid = dict(QUICK if args.quick else FULL)
    if not args.quick:
        grid.update({k: preset[k] for k in ("sizes", "blocks", "threads") if k in preset})
    for key in ("sizes", "blocks", "threads"):
        if getattr(args, key):
            grid[key] = getattr(args, key)
    scenario_sizes = {} if args.quick else dict(preset.get("scenario_sizes", {}))
    if args.sizes:
        scenario_sizes = {}
    if args.pause_runs is None:
        args.pause_runs = 0 if args.quick else preset.get("pause_runs", 120)
    if args.pause_scenarios is None:
        args.pause_scenarios = 0 if args.quick else preset.get("pause_scenarios", 15)

    runner = find_runner(args.build_dir)
    scenarios = [s for s in args.scenarios.split(",") if s]
    for s in scenarios:
        if s not in SCENARIOS:
            sys.exit(f"Неизвестный сценарий: {s}. Доступны: {', '.join(SCENARIOS)}")

    print(f"Раннер: {runner}\nСетка: {grid}\nРазмеры для отдельных сценариев: {scenario_sizes or 'нет'}")
    print(f"Прогонов: {args.runs}, паузы: {args.pause_runs} с между прогонами, "
          f"{args.pause_scenarios} с между сценариями\nСценарии: {', '.join(scenarios)}\n")
    started = time.time()

    for run in range(1, args.runs + 1):
        run_dir = args.out / f"run_{run}"
        run_dir.mkdir(parents=True, exist_ok=True)
        print(f"=== Прогон {run}/{args.runs} ===", flush=True)

        for i, scenario in enumerate(scenarios):
            csv_path = run_dir / f"{scenario}.csv"
            done_marker = run_dir / f".{scenario}.done"
            if args.resume and done_marker.exists():
                print(f"  [{scenario}] уже готов, пропуск")
                continue

            print(f"  [{scenario}] {SCENARIOS[scenario]}", flush=True)
            t0 = time.time()
            cmd = [str(runner), "--scenario", scenario, "--out", str(csv_path),
                   "--sizes", scenario_sizes.get(scenario, grid["sizes"]),
                   "--blocks", grid["blocks"], "--threads", grid["threads"]]
            result = subprocess.run(cmd, stderr=subprocess.DEVNULL)
            if result.returncode != 0:
                sys.exit(f"Сценарий {scenario} завершился с кодом {result.returncode}")
            done_marker.touch()
            print(f"  [{scenario}] готово за {time.time() - t0:.0f} с -> {csv_path}", flush=True)

            if i + 1 < len(scenarios):
                countdown(args.pause_scenarios, "между сценариями")

        if run < args.runs:
            countdown(args.pause_runs, "между прогонами")

    print(f"\nГотово за {(time.time() - started) / 60:.1f} мин. Результаты: {args.out}")
    print("Графики: python3 experiments/plot_results.py")


if __name__ == "__main__":
    main()
