import subprocess
import time
import pandas as pd
from typing import Dict, List, DefaultDict

from helpers import (
    Settings,
    create_settings,
    nix_build,
    spawn,
    read_stats,
    write_stats,
    NOW,
    scone_env
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
        #self.remote_redis = settings.remote_command(nix_build("redis-cli"))
        self.nc_command = settings.remote_command(nix_build("netcat-native"))
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
        args = [
            "bin/redis-server",
            "--dir", db_dir,
            "--tls-port", "6379",
            "--port", "0",
            "--tls-cert-file", f"{db_dir}/server.cert",
            "--tls-key-file", f"{db_dir}/server.key",
            "--tls-ca-cert-file", f"{db_dir}/ca.crt",
            "--requirepass", "snakeoil",
            "--tls-auth-clients", "no"
        ]
        with spawn(redis_server, *args, extra_env=extra_env) as proc:
            print(f"waiting for redis for {system} benchmark...", end="")
            while True:
                try:
                    #self.remote_redis.run(
                    #    self.remote_redis.nix_path, ["bin/redis-cli", "--tls",
                    #                      "--cert", "/proc/self/cwd/server.cert",
                    #                      "--key", "/proc/self/cwd/server.key",
                    #                      "--cacert", "/proc/self/cwd/ca.crt",
                    #                      "-h", self.settings.local_dpdk_ip, "ping"]
                    #)
                    nc_proc = self.nc_command.run("bin/nc", ["-z", "-v", self.settings.local_dpdk_ip, "6379"])
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
                    "redis.timeout=600000",
                    "-p",
                    f"recordcount={self.record_count}",
                    "-p",
                    f"operationcount={self.operation_count}",
                    "-p",
                    "redis.password=snakeoil",
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
                    "redis.timeout=600000",
                    "-p",
                    f"recordcount={self.record_count}",
                    "-p",
                    f"operationcount={self.operation_count}",
                    "-p",
                    "redis.password=snakeoil",
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


def benchmark_redis_scone(
    benchmark: Benchmark, stats: DefaultDict[str, List[str]]
) -> None:
    mount = benchmark.storage.setup(StorageKind.SCONE)
    redis_server = nix_build("redis-scone")

    with mount as mnt:
        extra_env = scone_env(mnt)
        extra_env.update(benchmark.network.setup(NetworkKind.NATIVE))
        extra_env.update(mount.extra_env())

        benchmark.run("scone", redis_server, mnt, stats, extra_env=extra_env)


def main() -> None:
    stats = read_stats("redis.json")
    settings = create_settings()
    setup_remote_network(settings)
    record_count = 1000000
    op_count = 10000000

    benchmark = Benchmark(settings, record_count, op_count)

    benchmarks = {
        "native": benchmark_redis_native,
        "sgx-lkl": benchmark_redis_sgx_lkl,
        "sgx-io": benchmark_redis_sgx_io,
        "scone": benchmark_redis_scone,
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
