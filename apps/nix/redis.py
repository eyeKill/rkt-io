import subprocess
import time
import pandas as pd
from io import StringIO
from typing import Dict, List, DefaultDict

from helpers import (
    Settings,
    create_settings,
    nix_build,
    spawn,
    read_stats,
    write_stats,
    NOW,
)
from storage import Storage, StorageKind
from network import Network, NetworkKind, setup_remote_network


def process_ycsb_out(ycsb_out: str, system: str, results: Dict[str, List]) -> None:
    for line in ycsb_out.split("\n"):
        if line == "":
            break
        operation, metric, value = line.split(",")
        results["system"].append(system)
        results["operation"].append(operation.strip())
        results["metric"].append(metric.strip())
        results["value"].append(value.strip())


class Benchmark:
    def __init__(self, settings: Settings, record_count: int, operation_count: int) -> None:
        self.settings = create_settings()
        self.storage = Storage(settings)
        self.network = Network(settings)
        self.remote_redis = settings.remote_command(nix_build("redis-cli"))
        self.remote_ycsb = settings.remote_command(nix_build("ycsb-native"))
        self.record_count = record_count
        self.operation_count = operation_count

    def run(
        self,
        system: str,
        redis_server: str,
        db_dir: str,
        stats: Dict[str, List],
        extra_env: Dict[str, str],
    ) -> None:
        env = extra_env.copy()
        env[
            "SGXLKL_SYSCTL"
        ] = "net.core.rmem_max=56623104;net.core.wmem_max=56623104;net.core.rmem_default=56623104;net.core.wmem_default=56623104;net.core.optmem_max=40960;net.ipv4.tcp_rmem=4096 87380 56623104;net.ipv4.tcp_wmem=4096 65536 56623104;"

        args = ["bin/redis-server", "--dir", db_dir, "--protected-mode", "no"]
        with spawn(redis_server, *args, extra_env=extra_env) as proc:
            print(f"waiting for redis for {system} benchmark...", end="")
            while True:
                try:
                    self.remote_redis.run(
                        "bin/redis-cli", ["-h", self.settings.local_dpdk_ip, "ping"]
                    )
                    break
                except subprocess.CalledProcessError:
                    status = proc.poll()
                    if status is not None:
                        raise OSError(f"redis-server exiteded with {status}")
                    time.sleep(1)
                    pass

            load_proc = self.remote_ycsb.run(
                "bin/ycsb",
                [
                    "load",
                    "redis",
                    "-s",
                    "-P",
                    f"{self.remote_ycsb.nix_path}/share/ycsb/workloads/workloada",
                    "-p",
                    f"redis.host={self.settings.local_dpdk_ip}",
                    "-p",
                    "redis.port=6379",
                    "-p",
                    f"recordcount={self.record_count}",
                    "-p",
                    f"operationcount={self.operation_count}",
                ],
            )

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
                    "-p",
                    f"recordcount={self.record_count}",
                    "-p",
                    f"operationcount={self.operation_count}",
                ],
            )

        process_ycsb_out(run_proc.stdout, system, stats)


def benchmark_redis_native(benchmark: Benchmark, stats: Dict[str, List],) -> None:
    extra_env = benchmark.network.setup(NetworkKind.NATIVE)
    redis_server = nix_build("redis-native")
    mount = benchmark.storage.setup(StorageKind.NATIVE)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run(
            "native", redis_server, mnt, stats, extra_env=extra_env,
        )


def benchmark_redis_sgx_lkl(benchmark: Benchmark, stats: Dict[str, List],) -> None:
    extra_env = benchmark.network.setup(NetworkKind.TAP)
    redis_server = nix_build("redis-sgx-lkl")
    mount = benchmark.storage.setup(StorageKind.LKL)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run("sgx-lkl", redis_server, mnt, stats, extra_env=extra_env)


def benchmark_redis_sgx_io(
    benchmark: Benchmark, stats: DefaultDict[str, List[str]]
) -> None:
    extra_env = benchmark.network.setup(NetworkKind.DPDK)
    redis_server = nix_build("redis-sgx-io")
    mount = benchmark.storage.setup(StorageKind.SPDK)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run("sgx-io", redis_server, mnt, stats, extra_env=extra_env)


def main() -> None:
    stats = read_stats("redis.json")
    settings = create_settings()
    setup_remote_network(settings)
    record_count = 1000000
    op_count = 10000000

    benchmark = Benchmark(settings, record_count, op_count)

    benchmarks = {
        "native": benchmark_redis_native,
        "sgx-io": benchmark_redis_sgx_io,
        "sgx-lkl": benchmark_redis_sgx_lkl,
    }

    system = set(stats["system"])
    for name, benchmark_func in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark_func(benchmark, stats)
        write_stats("redis.json", stats)

    csv = f"redis-{NOW}.tsv"
    print(csv)
    throughput_df = pd.DataFrame(stats)
    throughput_df.to_csv(csv, index=False, sep="\t")
    throughput_df.to_csv("redis-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
