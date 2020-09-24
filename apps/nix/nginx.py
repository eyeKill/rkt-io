import time
import subprocess
from typing import Dict, List

import pandas as pd

from helpers import (
    NOW,
    Settings,
    create_settings,
    nix_build,
    read_stats,
    write_stats,
    spawn,
)
from storage import Storage, StorageKind
from network import Network, NetworkKind, setup_remote_network
from process_wrk import parse_wrk_output


def process_wrk_output(wrk_out: str, system: str, stats: Dict[str, List[str]], connections: int) -> None:
    wrk_metrics = parse_wrk_output(wrk_out)
    stats["system"].append(system)
    stats["connections"].append(str(connections))
    for k, v in wrk_metrics.items():
        stats[k].append(v)


class Benchmark:
    def __init__(self, settings: Settings) -> None:
        self.settings = create_settings()
        self.storage = Storage(settings)
        self.network = Network(settings)
        self.remote_nc = settings.remote_command(nix_build("netcat-native"))
        self.remote_wrk = settings.remote_command(nix_build("wrk-bench"))

    def run(
        self,
        attr: str,
        system: str,
        mnt: str,
        stats: Dict[str, List],
        extra_env: Dict[str, str] = {},
    ) -> None:
        env = extra_env.copy()
        env.update(dict(SGXLKL_CWD=mnt))

        nginx_server = nix_build(attr)
        host = self.settings.local_dpdk_ip
        with spawn(nginx_server, "bin/nginx", "-c", f"{mnt}/nginx/nginx.conf", extra_env=env) as proc:
            while True:
                try:
                    self.remote_nc.run(
                        "bin/nc", ["-z", self.settings.local_dpdk_ip, "9000"]
                    )
                    break
                except subprocess.CalledProcessError:
                    status = proc.poll()
                if status is not None:
                    raise OSError(f"nginx exiteded with {status}")
                    time.sleep(1)
                pass

            wrk_connections = 400
            wrk_proc = self.remote_wrk.run(
                "bin/wrk", ["-t", "12", "-c", f"{wrk_connections}", "-d", "30s", f"https://{host}:9000"]
            )
            process_wrk_output(wrk_proc.stdout, system, stats, wrk_connections)


def benchmark_nginx_native(
    benchmark: Benchmark, stats: Dict[str, List]
) -> None:
    extra_env = benchmark.network.setup(NetworkKind.NATIVE)
    mount = benchmark.storage.setup(StorageKind.NATIVE)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run("nginx-native", "native", mnt, stats, extra_env=extra_env)


def benchmark_nginx_sgx_lkl(
    benchmark: Benchmark, stats: Dict[str, List]
) -> None:
    extra_env = benchmark.network.setup(NetworkKind.TAP)
    mount = benchmark.storage.setup(StorageKind.LKL)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run("nginx-sgx-lkl", "sgx-lkl", mnt, stats, extra_env=extra_env)


def benchmark_nginx_sgx_io(
    benchmark: Benchmark, stats: Dict[str, List]
) -> None:
    extra_env = benchmark.network.setup(NetworkKind.DPDK)
    mount = benchmark.storage.setup(StorageKind.SPDK)
    extra_env.update(mount.extra_env())

    with mount as mnt:
        benchmark.run("nginx-sgx-io", "sgx-io", mnt, stats, extra_env=extra_env)


def main() -> None:
    stats = read_stats("nginx.json")
    settings = create_settings()
    setup_remote_network(settings)

    benchmark = Benchmark(settings)

    benchmarks = {
        "native": benchmark_nginx_native,
        "sgx-io": benchmark_nginx_sgx_io,
        "sgx-lkl": benchmark_nginx_sgx_lkl,
    }

    system = set(stats["system"])
    for name, benchmark_func in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark_func(benchmark, stats)
        write_stats("nginx.json", stats)

    csv = f"nginx-{NOW}.tsv"
    print(csv)
    throughput_df = pd.DataFrame(stats)
    throughput_df.to_csv(csv, index=False, sep="\t")
    throughput_df.to_csv("nginx-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
