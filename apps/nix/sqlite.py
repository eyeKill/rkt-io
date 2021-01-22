import os
import subprocess
import signal
from typing import Dict, List, Any
import re

import pandas as pd
from helpers import (
    NOW,
    create_settings,
    nix_build,
    read_stats,
    write_stats,
    scone_env,
    flamegraph_env
)
from storage import Storage, StorageKind


def benchmark_sqlite(
    storage: Storage,
    system: str,
    attr: str,
    directory: str,
    stats: Dict[str, List[Any]],
    extra_env: Dict[str, str] = {},
) -> None:
    env = os.environ.copy()
    del env["SGXLKL_TAP"]
    env.update(dict(SGXLKL_CWD=directory))
    env.update(extra_env)

    env.update(flamegraph_env(f"{os.getcwd()}/sqlite-{system}"))
    env.update(extra_env)

    sqlite = nix_build(attr)
    stdout = subprocess.PIPE
    cmd = [str(sqlite)]
    proc = subprocess.Popen(cmd, stdout=stdout, text=True, env=env)

    print(f"[Benchmark]:{system}")

    n_rows = 0
    try:
        if proc.stdout is None:
            proc.wait()
        else:
            for line in proc.stdout:
                line = line.rstrip()
                print(line)
                match = re.match(r"(?: \d+ - |\s+)([^.]+)[.]+\s+([0-9.]+)s", line)
                if match:
                    if "TOTAL" in match.group(1):
                        continue
                    stats["system"].append(system)
                    stats["sqlite-op-type"].append(match.group(1))
                    stats["sqlite-time [s]"].append(match.group(2))
                    n_rows += 1
                    if n_rows == 3:
                        break
    finally:
        proc.send_signal(signal.SIGINT)

    expected = 3
    if n_rows < expected:
        raise RuntimeError(f"Expected {expected} rows, got: {n_rows} when running benchmark for {system}")


def benchmark_sqlite_native(storage: Storage, stats: Dict[str, List[Any]]) -> None:
    mount = storage.setup(StorageKind.NATIVE)
    with mount as mnt:
        benchmark_sqlite(storage, "native", "sqlite-native", mnt, stats, extra_env=mount.extra_env())


def benchmark_sqlite_sgx_lkl(storage: Storage, stats: Dict[str, List[Any]]) -> None:
    mount = storage.setup(StorageKind.LKL)
    with mount as mnt:
        benchmark_sqlite(
            storage,
            "sgx-lkl",
            "sqlite-sgx-lkl",
            mnt,
            stats,
            extra_env=mount.extra_env())


def benchmark_sqlite_sgx_io(storage: Storage, stats: Dict[str, List[Any]]) -> None:
    mount = storage.setup(StorageKind.SPDK)
    with mount as mnt:
        benchmark_sqlite(storage, "sgx-io", "sqlite-sgx-io", mnt, stats, extra_env=mount.extra_env())


def benchmark_sqlite_scone(storage: Storage, stats: Dict[str, List[Any]]) -> None:
    mount = storage.setup(StorageKind.SCONE)
    with mount as mnt:
        extra_env = scone_env(mnt)
        extra_env.update(mount.extra_env())
        benchmark_sqlite(storage, "scone", "sqlite-scone", mnt, stats, extra_env=extra_env)


def main() -> None:
    stats = read_stats("sqlite.json")
    settings = create_settings()
    storage = Storage(settings)

    benchmarks = {
        "scone": benchmark_sqlite_scone,
        "native": benchmark_sqlite_native,
        "sgx-lkl": benchmark_sqlite_sgx_lkl,
        "sgx-io": benchmark_sqlite_sgx_io,
    }
    system = set(stats["system"])
    for name, benchmark in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark(storage, stats)
        write_stats("sqlite.json", stats)

    csv = f"sqlite-speedtest-{NOW}.tsv"
    print(csv)
    df = pd.DataFrame(stats)
    df.to_csv(csv, index=False, sep="\t")
    df.to_csv("sqlite-speedtest-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
