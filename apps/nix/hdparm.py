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


def benchmark_hdparm(
    storage: Storage,
    system: str,
    attr: str,
    device: str,
    stats: Dict[str, List],
    extra_env: Dict[str, str] = {},
) -> None:
    env = os.environ.copy()
    env.update(flamegraph_env(f"hdparm-{system}-{NOW}"))
    env.update(extra_env)
    hdparm = nix_build(attr)
    print(f"###### {system} >> ######")
    proc = subprocess.Popen(["sudo", hdparm, "bin/hdparm", "-Tt", device], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        if proc.stdout is None:
            proc.wait()
        else:
            stats["system"].append(system)
            for line in proc.stdout:
                print(line)
                match = re.match(r"(.*):\s+(.*) = (.*)", line)
                if match:
                    stats[match.group(1)].append(match.group(3))
    finally:
        #proc.send_signal(signal.SIGINT)
        pass
    print(f"###### {system} << ######")


def benchmark_hdparm_native(storage: Storage, stats: Dict[str, List]) -> None:
    mnt = storage.setup(StorageKind.NATIVE)
    subprocess.run(["sudo", "chown", getpass.getuser(), mnt.dev])
    benchmark_hdparm(storage, "native", "hdparm-native", mnt.dev, stats)


def benchmark_hdparm_sgx_lkl(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.LKL)
    benchmark_hdparm(
        storage,
        "sgx-lkl",
        "hdparm",
        "/dev/vdb",
        stats,
        extra_env=dict(SGXLKL_HDS="/dev/nvme0n1:/mnt/nvme"),
    )


def benchmark_hdparm_sgx_io(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.SPDK)
    benchmark_hdparm(storage, "sgx-io", "hdparm", "/dev/spdk0", stats)


def main() -> None:
    stats = read_stats("hdparm.json")
    settings = create_settings()
    storage = Storage(settings)

    benchmarks = {
        "native": benchmark_hdparm_native,
        "sgx-lkl": benchmark_hdparm_sgx_lkl,
        "sgx-io": benchmark_hdparm_sgx_io,
    }

    system = set(stats["system"])
    for name, benchmark in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark(storage, stats)
        write_stats("sqlite.json", stats)

    csv = f"hdparm-test-{NOW}.tsv"
    print(csv)
    df = pd.DataFrame(stats)
    df.to_csv(csv, index=False, sep="\t")
    df.to_csv("hdparm-test-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
