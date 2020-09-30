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


def benchmark_fio(
    system: str,
    attr: str,
    cores: int,
    directory: str,
    stats: Dict[str, List],
    extra_env: Dict[str, str] = {},
) -> None:

    env = os.environ.copy()
    # we don't need network for these benchmarks
    del env["SGXLKL_TAP"]
    env.update(dict(SGXLKL_CWD=directory))
    env.update(flamegraph_env(f"fio-{system}-{NOW}"))
    env.update(extra_env)
    enable_sgxio = "1" if system == "sgx-io" else "0"
    env.update(SGXLKL_ENABLE_SGXIO=enable_sgxio)
    env.update(SGXLKL_ETHREADS=str(cores))
    env.update(extra_env)
    fio = nix_build(attr)
    stdout: Optional[int] = subprocess.PIPE
    if os.environ.get("SGXLKL_ENABLE_GDB", "0") == "1":
        stdout = None

    cmd = [str(fio), "bin/fio", "--output-format=json", "--eta=always", f"fio-rand-RW-smp-{cores}.job"]
    proc = subprocess.Popen(cmd, stdout=stdout, text=True, env=env)
    data = ""
    in_json = False
    print(f"[Benchmark]: {system}")
    try:
        if proc.stdout is None:
            proc.wait()
        else:
            for line in proc.stdout:
                print(line, end="")
                if line == "{\n":
                    in_json = True
                if in_json:
                    data += line
                if line == "}\n":
                    break
    finally:
        proc.send_signal(signal.SIGINT)
        proc.wait()
    if data == "":
        raise RuntimeError(f"Did not get a result when running benchmark for {system}")
    jsondata = json.loads(data)
    for jobnum, job in enumerate(jsondata["jobs"]):
        stats["system"].append(system)
        stats["job"].append(jobnum)
        stats["cores"].append(cores)
        for op in ["read", "write", "trim"]:
            metrics = job[op]
            for metric_name, metric in metrics.items():
                if isinstance(metric, dict):
                    for name, submetric in metric.items():
                        stats[f"{op}-{metric_name}-{name}"].append(submetric)
                else:
                    stats[f"{op}-{metric_name}"].append(metric)


def benchmark_sgx_io(storage: Storage, stats: Dict[str, List], cores: int) -> None:
    mount = storage.setup(StorageKind.SPDK)
    with mount as mnt:
        benchmark_fio("sgx-io", "fio-sgx-io", cores, mnt, stats, extra_env=mount.extra_env())


def main() -> None:
    stats = read_stats("smp.json")

    settings = create_settings()

    storage = Storage(settings)

    for cores in [1, 2, 4, 6, 8]:
        benchmark_sgx_io(storage, stats, cores)
        write_stats("smp.json", stats)

    csv = f"smp-{NOW}.tsv"
    print(csv)
    throughput_df = pd.DataFrame(stats)
    throughput_df.to_csv(csv, index=False, sep="\t")
    throughput_df.to_csv("smp-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
