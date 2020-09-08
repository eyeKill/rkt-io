#!/usr/bin/env python3
import json
import os
import getpass
import subprocess
import tempfile
import time
from collections import defaultdict
from typing import Any, DefaultDict, Dict, List, Union
import re
import signal
import pandas as pd

from helpers import (
    NOW,
    ROOT,
    Chdir,
    Settings,
    create_settings,
    nix_build,
    run,
    spawn,
    flamegraph_env,
    read_stats,
    write_stats
)
from storage import Storage, StorageKind

def benchmark_dd(
    storage: Storage,
    system: str,
    attr: str,
    device: str,
    stats: Dict[str, List],
    extra_env: Dict[str, str] = {},
) -> None:
    env = os.environ.copy()
    env.update(extra_env)
    dd = nix_build(attr)

    print(f"###### {system} >> ######")
    proc = subprocess.Popen([dd], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    try:
        if proc.stdout is None:
            proc.wait()
        else:
            stats["system"].append(system)
            for line in proc.stdout:
                data = line.split(',')
                if len(data) == 2:
                    stats["latency"].append(data[0])
                    stats["Throughput"].append(data[1])
    finally:
        pass

    print(f"###### {system} << ######")

def benchmark_native_dd(storage: Storage, stats: Dict[str, List]) -> None:
    mnt = storage.setup(StorageKind.NATIVE)
    subprocess.run(["sudo", "chown", getpass.getuser(), mnt.dev])
    benchmark_dd(storage, "native", "dd-native", mnt.dev, stats)

def benchmark_sgx_lkl_dd(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.LKL)
    benchmark_dd(
      storage,
      "sgx-lkl",
      "dd-sgx-lkl",
      "/dev/vdb",
      stats,
      extra_env=dict(SGXLKL_HDS="/dev/nvme0n1:/mnt/nvme"),
    )

def benchmark_sgx_io_dd(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.SPDK)
    benchmark_dd(storage, "sgx-io", "dd-sgx-io", "/dev/spdk0", stats)

def main() -> None:
    stats = read_stats("dd.json")
    settings = create_settings()
    storage = Storage(settings)

    # provide nix expressions for native and sgx_lkl case
    benchmarks = {
        #"native": benchmark_native_dd,
        #"sgx-lkl": benchmark_sgx_lkl_dd,
        "sgx-io": benchmark_sgx_io_dd,
    }

    system = set(stats["system"])

    for name, benchmark in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark(storage, stats)
        write_stats("dd.json", stats)

    csv = f"dd-test-{NOW}.tsv"
    df = pd.DataFrame(stats)
    df.to_csv(csv, index=False, sep="\t")
    df.to_csv("dd-test-latest.tsv", index=False, sep="\t")

if __name__=="__main__":
    main()
