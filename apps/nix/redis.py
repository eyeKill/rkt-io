import subprocess
import time
import pandas as pd
from io import StringIO
from typing import Dict, List

from helpers import (
    Settings,
    create_settings,
    nix_build,
    spawn,
)
from storage import Storage, StorageKind
from network import Network, NetworkKind, setup_remote_network


def process_ycsb_out(ycsb_out: str, system: str, bench_result: List[str]) -> None:
    csv_file = StringIO(ycsb_out)
    df = pd.read_csv(csv_file, header=None)
    csv_headers = list(df.iloc[:, 1].values)
    csv_vals = list(df.iloc[:, 2].values)
    csv_vals = [str(i) for i in csv_vals]

    if len(bench_result) == 0:
        bench_result.append(",".join(["system"] + csv_headers))

    bench_result.append(",".join([system] + csv_vals))


class Benchmark:
    def __init__(self, settings: Settings) -> None:
        self.settings = create_settings()
        self.storage = Storage(settings)
        self.network = Network(settings)
        self.remote_ycsb = settings.remote_command(nix_build("ycsb-native"))

    def run(
        self,
        system: str,
        redis_server: str,
        db_dir: str,
        stats: Dict[str, List[str]],
        extra_env: Dict[str, str],
    ) -> None:
        env = extra_env.copy()
        env[
            "SGXLKL_SYSCTL"
        ] = "net.core.rmem_max=56623104;net.core.wmem_max=56623104;net.core.rmem_default=56623104;net.core.wmem_default=56623104;net.core.optmem_max=40960;net.ipv4.tcp_rmem=4096 87380 56623104;net.ipv4.tcp_wmem=4096 65536 56623104;"

        with spawn(redis_server, "--dir", db_dir, extra_env=extra_env):
            while True:
                try:
                    print(f"Running wrk benchmark for {system} case")
                    args = [
                        "load",
                        "redis",
                        "-s",
                        "-P",
                        f"{self.remote_ycsb.nix_path}/share/ycsb/workloads/workloada",
                        "-p",
                        f"redis.host={self.settings.local_dpdk_ip}",
                        "-p",
                        "redis.port=6379",
                    ]
                    load_proc = self.remote_ycsb.run("bin/ycsb", args)
                    break
                except subprocess.CalledProcessError:
                    time.sleep(5)
                    print(".")
                    pass

            run_proc = self.remote_ycsb.run(
                "bin/ycsb",
                [
                    "run",
                    "redis",
                    "-s",
                    "-P",
                    f"{self.remote_ycsb.nix_path}/share/ycsb/workloads/workloada",
                    "-p",
                    f"redis.host={self.settings.local_dpdk_ip}",
                    "-p",
                    "redis.port=6379",
                ],
            )

        if "load_res" not in stats:
            stats["load_res"] = []
        if "run_res" not in stats:
            stats["run_res"] = []

        process_ycsb_out(load_proc.stdout, system, stats["load_res"])
        process_ycsb_out(run_proc.stdout, system, stats["run_res"])


def benchmark_redis_native(
        benchmark: Benchmark,
        stats: Dict[str, List[str]],
) -> None:
    extra_env = benchmark.network.setup(NetworkKind.NATIVE)
    redis_server = nix_build("redis-native")
    mount = benchmark.storage.setup(StorageKind.NATIVE)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run(
            "native",
            redis_server,
            mnt,
            stats,
            extra_env=extra_env,
        )


def benchmark_redis_sgx_lkl(
        benchmark: Benchmark,
        stats: Dict[str, List[str]],
) -> None:
    extra_env = benchmark.network.setup(NetworkKind.TAP)
    redis_server = nix_build("redis-sgx-lkl")
    mount = benchmark.storage.setup(StorageKind.LKL)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run(
            "native", redis_server, mnt, stats, extra_env=extra_env
        )


def benchmark_redis_sgx_io(
        benchmark: Benchmark, stats: Dict[str, List[str]]
) -> None:
    extra_env = benchmark.network.setup(NetworkKind.DPDK)
    redis_server = nix_build("redis-sgx-io")
    mount = benchmark.storage.setup(StorageKind.LKL)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run(
            "native", redis_server, mnt, stats, extra_env=extra_env
        )


def main() -> None:
    stats: Dict[str, List[str]] = {}
    settings = create_settings()
    setup_remote_network(settings)

    benchmark = Benchmark(settings)

    benchmark_redis_native(benchmark, stats)
    benchmark_redis_sgx_lkl(benchmark, stats)
    benchmark_redis_sgx_io(benchmark, stats)

    load_df = pd.DataFrame(stats["load_res"])
    run_df = pd.DataFrame(stats["run_res"])

    load_df.to_csv("redis_load.csv")
    run_df.to_csv("redis_run.csv")


if __name__ == "__main__":
    main()
