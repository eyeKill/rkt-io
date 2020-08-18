import json
import os
import subprocess
import signal
from typing import Dict, List, Optional

import pandas as pd
from helpers import (
    NOW,
    create_settings,
    flamegraph_env,
    nix_build,
    read_stats,
    write_stats,
)
from storage import Storage, StorageKind


def benchmark_sqlite(
    storage: Storage,
    system: str,
    attr: str,
    directory: str,
    stats: Dict[str, List[str]],
    extra_env: Dict[str, str] = {},
) -> None:
    env = os.environ.copy()
    del env["SGXLKL_TAP"]
    env.update(dict(SGXLKL_CWD=directory))
    env.update(extra_env)

    enable_sgxio = "1" if system == "sgx-io" else "0"
    env.update(SGXLKL_ENABLE_SGXIO=enable_sgxio)
    threads = "8" if system == "sgx-io" else "2"
    env.update(SGXLKL_ETHREADS=threads)
    env.update(extra_env)

    sqlite = nix_build(attr)
    stdout = subprocess.PIPE
    cmd = [str(sqlite)]
    proc = subprocess.Popen(cmd, stdout=stdout, text=True, env=env)

    print(f"[Benchmark]:{system}")
    data = []

    try:
        if proc.stdout is None:
            proc.wait()
        else:
            for line in proc.stdout:
                line = line.split(" ")[-1]
                data.append(line.replace("\n", ""))
    finally:
        proc.send_signal(signal.SIGINT)

    if data == []:
        raise RuntimeError(f"Did not get a result when running benchmark for {system}")

    data[0] = system
    stats[system] = data


def benchmark_sqlite_native(storage: Storage, stats: Dict[str, List[str]]) -> None:
    with storage.setup(StorageKind.NATIVE) as mnt:
        benchmark_sqlite(storage, "native", "sqlite-native", mnt, stats)


def benchmark_sqlite_sgx_lkl(storage: Storage, stats: Dict[str, List[str]]) -> None:
    storage.setup(StorageKind.LKL)
    benchmark_sqlite(
        storage,
        "sgx-lkl",
        "sqlite",
        "/mnt/nvme",
        stats,
        extra_env=dict(SGXLKL_HDS="/dev/nvme0n1:/mnt/nvme"),
    )


def benchmark_sqlite_sgx_io(storage: Storage, stats: Dict[str, List[str]]) -> None:
    storage.setup(StorageKind.SPDK)
    benchmark_sqlite(storage, "sgx-io", "sqlite", "/mnt/spdk0", stats)


def main() -> None:
    settings = create_settings()
    storage = Storage(settings)
    stats: Dict[str, List[str]] = {}

    benchmark_sqlite_native(storage, stats)
    benchmark_sqlite_sgx_lkl(storage, stats)
    benchmark_sqlite_sgx_io(storage, stats)

    aggr_data = []
    for i in stats:
        aggr_data.append(stats[i])

    csv = f"sqlite-speedtest-{NOW}.tsv"
    df = pd.DataFrame(aggr_data)
    df.to_csv(csv, index=False, sep="\t")


if __name__ == "__main__":
    main()
