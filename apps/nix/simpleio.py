import os
import json
import sys
import signal
from typing import Dict, List, Optional
import subprocess
import pandas as pd

from helpers import (
    NOW,
    create_settings,
    flamegraph_env,
    nix_build,
    read_stats,
    write_stats,
    scone_env,
)
from storage import Storage, StorageKind


def benchmark_simpleio(
    storage: Storage,
    system: str,
    attr: str,
    directory: str,
    stats: Dict[str, List],
    bs: int,
    extra_env: Dict[str, str] = {},
    do_write: bool = False,
) -> None:
    env = dict(SGXLKL_CWD=directory)
    env.update(flamegraph_env(f"simpleio-{system}-{NOW}"))
    env.update(extra_env)
    enable_sgxio = "1" if system == "sgx-io" else "0"
    env.update(SGXLKL_ENABLE_SGXIO=enable_sgxio)
    threads = "1" if system == "sgx-io" else "8"
    env.update(SGXLKL_ETHREADS=threads)
    env.update(extra_env)
    simpleio = nix_build(attr)
    stdout: Optional[int] = subprocess.PIPE
    if os.environ.get("SGXLKL_ENABLE_GDB", "0") == "1":
        stdout = None

    size = str(10 * 1024 * 1024 * 1024)  # 10G


    env_string = []
    for k, v in env.items():
        env_string.append(f"{k}={v}")
    report = ""
    in_results = False
    env = os.environ.copy()
    env.update(extra_env)

    cmd = [
        simpleio,
        "bin/simpleio",
        f"{directory}/file",
        size,
        "0" if do_write else "1",
        "1",
        str(bs * 1024),
    ]
    print(f"$ {' '.join(env_string)} {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, stdout=stdout, text=True, env=env)
    try:
        assert proc.stdout is not None
        for line in proc.stdout:
            print(f"stdout: {line}", end="")
            if line == "<result>\n":
                in_results = True
            elif in_results and line == "</result>\n":
                break
            elif in_results:
                report = line
    finally:
        proc.send_signal(signal.SIGINT)
    jsondata = json.loads(report)
    stats["system"].append(system)
    stats["bytes"].append(jsondata["bytes"])
    stats["time"].append(jsondata["time"])
    stats["workload"].append("write" if do_write else "read")
    stats["batch-size"].append(bs)


def benchmark_sgx_io(storage: Storage, stats: Dict[str, List], bs:int) -> None:
    mount = storage.setup(StorageKind.SPDK)

    with mount as mnt:
        benchmark_simpleio(
            storage, "sgx-io", "simpleio-sgx-io", mnt, stats, bs, extra_env=mount.extra_env()
        )


#def benchmark_sgx_lkl(storage: Storage, stats: Dict[str, List]) -> None:
#    mount = storage.setup(StorageKind.LKL)
#
#    with mount as mnt:
#        benchmark_simpleio(
#            storage,
#            "sgx-lkl",
#            "simpleio-sgx-lkl",
#            mnt,
#            stats,
#            extra_env=mount.extra_env(),
#        )
#
#
#def benchmark_scone(storage: Storage, stats: Dict[str, List]) -> None:
#    with storage.setup(StorageKind.NATIVE) as mnt:
#        benchmark_simpleio(storage, "scone", "simpleio-scone", mnt, stats, extra_env=scone_env(mnt))
#
#
#def benchmark_native(storage: Storage, stats: Dict[str, List]) -> None:
#    with storage.setup(StorageKind.NATIVE) as mnt:
#        benchmark_simpleio(storage, "native", "simpleio-native", mnt, stats)


BENCHMARKS = {
    #"native": benchmark_native,
    "sgx-io": benchmark_sgx_io,
    #"scone": benchmark_scone,
    #"sgx-lkl": benchmark_sgx_lkl,
}


def main() -> None:
    if len(sys.argv) <= 1:
        benchmarks = BENCHMARKS
    else:
        benchmarks = {k: BENCHMARKS[k] for k in (sys.argv[1:])}

    stats = read_stats("simpleio-stats.json")
    storage = Storage(create_settings())
    system = set(stats["system"])

    # batch sizes in kilobytes
    batch_sizes = [4, 8, 16, 32, 64, 128, 256, 512]
    #batch_sizes = [128]

    for bs in batch_sizes:
        for name, benchmark in benchmarks.items():
            if name in system and len(sys.argv) <= 1:
                print(f"skip {name} benchmark")
                continue
            benchmark(storage, stats, bs)
            write_stats("simpleio-stats.json", stats)

    csv = f"simpleio-{NOW}.tsv"
    print(csv)
    df = pd.DataFrame(stats)
    df.to_csv(csv, index=False, sep="\t")
    df.to_csv("simpleio-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
