import getpass
import json
import os
import subprocess
import tempfile
import time
from collections import defaultdict
from typing import Any, DefaultDict, Dict, List, Optional, Union

import pandas as pd
from helpers import (
    NOW,
    ROOT,
    Chdir,
    Settings,
    create_settings,
    flamegraph_env,
    nix_build,
    run,
    spawn,
    read_stats,
    write_stats
)
from storage import Storage, StorageKind


def percentile(idx: int, run_total: List[int]) -> float:
    total = run_total[len(run_total) - 1]
    if total == 0:
        return 0

    return float(run_total[idx]) / total


def more_lines(indices: Dict[str, int], bins: Dict[str, List[List[Any]]]) -> bool:
    for key, value in indices.items():
        if value < len(bins[key]):
            return True

    return False


def parse_json_plus(
    jsondata: Dict[str, Any], system: str, stats: Dict[str, List]
) -> None:
    for jobnum in range(0, len(jsondata["jobs"])):
        bins = {}
        run_total = {}
        operation_set = set(["read", "write", "trim"])

        prev_operation = None
        for operation in operation_set:
            if "bins" in jsondata["jobs"][jobnum][operation]["clat_ns"]:
                bins_loc = "clat_ns"
            elif "bins" in jsondata["jobs"][jobnum][operation]["lat_ns"]:
                bins_loc = "lat_ns"
            else:
                raise RuntimeError(
                    "Latency bins not found. "
                    "Are you sure you are using json+ output?"
                )

            bin = []
            for key, value in jsondata["jobs"][jobnum][operation][bins_loc][
                "bins"
            ].items():
                bin.append([int(key), value])
            bins[operation] = bin
            bins[operation] = sorted(bins[operation], key=lambda bin: bin[0])

            run_total[operation] = [0 for x in range(0, len(bins[operation]))]
            if len(bins[operation]) > 0:
                run_total[operation][0] = bins[operation][0][1]
                for x in range(1, len(bins[operation])):
                    run_total[operation][x] = (
                        run_total[operation][x - 1] + bins[operation][x][1]
                    )

        # Have a counter for each operation
        # In each round, pick the shortest remaining duration
        # and output a line with any values for that duration
        indices = {x: 0 for x in operation_set}
        while more_lines(indices, bins):
            min_lat = 17_112_760_320
            for operation in operation_set:
                if indices[operation] < len(bins[operation]):
                    min_lat = min(bins[operation][indices[operation]][0], min_lat)

            stats["job"].append(jobnum)
            stats["system"].append(system)
            stats[f"{bins_loc}ec"].append(min_lat)

            for operation in operation_set:
                if (
                    indices[operation] < len(bins[operation])
                    and min_lat == bins[operation][indices[operation]][0]
                ):
                    count = bins[operation][indices[operation]][1]
                    cumulative = run_total[operation][indices[operation]]
                    ptile = percentile(indices[operation], run_total[operation])
                    stats[f"{operation}_count"].append(count)
                    stats[f"{operation}_cumulative"].append(cumulative)
                    stats[f"{operation}_percentile"].append(ptile)
                    indices[operation] += 1
                else:
                    stats[f"{operation}_count"].append(None)
                    stats[f"{operation}_cumulative"].append(None)
                    stats[f"{operation}_percentile"].append(None)


def benchmark_fio(
    storage: Storage,
    system: str,
    attr: str,
    directory: str,
    stats: Dict[str, List],
    latency_stats: Dict[str, List],
    extra_env: Dict[str, str] = {},
):
    env = dict(SGXLKL_CWD=directory)
    env.update(flamegraph_env(f"fio-{system}-{NOW}"))
    env.update(extra_env)
    enable_sgxio = "1" if system == "sgx-io" else "0"
    env.update(SGXLKL_ENABLE_SGXIO=enable_sgxio)
    threads = "8" if system == "sgx-io" else "2"
    env.update(SGXLKL_ETHREADS=threads)
    env.update(extra_env)
    fio = nix_build(attr)
    stdout: Optional[int] = subprocess.PIPE
    if os.environ.get("SGXLKL_ENABLE_GDB", "0") == "1":
        stdout = None

    proc = run(
        [
            fio,
            "bin/fio",
            # "--debug=io",
            "--output-format=json+",
            "--eta=always",
            "fio-rand-RW.job"
            # "fio-seq-RW.job"
        ],
        extra_env=env,
        stdout=stdout,
    )
    try:
        jsondata = json.loads(proc.stdout)
    except json.decoder.JSONDecodeError:
        breakpoint()
        print(proc.stdout)
        raise
    parse_json_plus(jsondata, system, latency_stats)

    operation_set = set(["read", "write", "trim"])
    for jobnum in range(0, len(jsondata["jobs"])):
        stats["system"].append(system)
        stats["job"].append(jobnum)

        for operation in operation_set:
            op_stats = jsondata["jobs"][jobnum][operation]
            stats[f"{operation}-iobytes"].append(op_stats["io_bytes"])
            stats[f"{operation}-iops"].append(op_stats["iops"])
            stats[f"{operation}-runtime"].append(op_stats["runtime"])


def benchmark_native(
    storage: Storage, stats: Dict[str, List], latency_stats: Dict[str, List]
) -> None:
    with storage.setup(StorageKind.NATIVE) as mnt:
        benchmark_fio(storage, "native", "fio-native", mnt, stats, latency_stats)


def benchmark_scone(
    storage: Storage, stats: Dict[str, List], latency_stats: Dict[str, List]
) -> None:
    with storage.setup(StorageKind.NATIVE) as mnt:
        benchmark_fio(storage, "scone", "fio-scone", mnt, stats, latency_stats)


def benchmark_sgx_lkl(
    storage: Storage, stats: Dict[str, List], latency_stats: Dict[str, List]
) -> None:
    storage.setup(StorageKind.LKL)
    benchmark_fio(
        storage,
        "sgx-lkl",
        "fio",
        "/mnt/nvme",
        stats,
        latency_stats,
        extra_env=dict(SGXLKL_HDS="/dev/nvme0n1:/mnt/nvme"),
    )


def benchmark_sgx_io(
    storage: Storage, stats: Dict[str, List], latency_stats: Dict[str, List]
) -> None:
    storage.setup(StorageKind.SPDK)
    benchmark_fio(storage, "sgx-io", "fio", "/mnt/spdk0", stats, latency_stats)


def main() -> None:
    stats = read_stats("stats.json")
    latency_stats = read_stats("latency-stats.json")

    settings = create_settings()

    storage = Storage(settings)

    system = set(stats["system"])

    benchmarks = {
        "native": benchmark_native,
        "sgx-io": benchmark_sgx_io,
        "scone": benchmark_scone,
        "sgx-lkl": benchmark_sgx_lkl,
    }

    for name, benchmark in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark(storage, stats, latency_stats)
        write_stats("stats.json", stats)
        write_stats("latency-stats.json", latency_stats)

    csv = f"fio-throughput-{NOW}.tsv"
    print(csv)
    throughput_df = pd.DataFrame(stats)
    throughput_df.to_csv(csv, index=False, sep="\t")
    throughput_df.to_csv("fio-throughput-latest.tsv", index=False, sep="\t")

    csv = f"fio-latency-{NOW}.tsv"
    print(csv)
    latency_df = pd.DataFrame(latency_stats)
    latency_df.to_csv(csv, index=False, sep="\t")
    latency_df.to_csv("fio-latency-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
