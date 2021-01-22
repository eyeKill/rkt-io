#!/usr/bin/env python3

import sys
import json
import subprocess
import time
import signal
from functools import lru_cache
from typing import Any, Dict, List

import pandas as pd
from helpers import (
    read_stats,
    write_stats,
    NOW,
    Settings,
    create_settings,
    flamegraph_env,
    nix_build,
    spawn,
    RemoteCommand
)
from network import Network, NetworkKind, setup_remote_network


def _postprocess_iperf(
    raw_data: Dict[str, Any], direction: str, system: str, stats: Dict[str, Any]
) -> None:
    for instance in raw_data["instances"]:
        result = instance["result"]
        if "error" in result:
            print(result["error"], file=sys.stderr)
            sys.exit(1)
        cpu = result["end"]["cpu_utilization_percent"]

        for interval in result["intervals"]:
            for key in cpu.keys():
                stats[f"cpu_{key}"].append(cpu[key])

            moved_bytes = 0
            seconds = 0.0
            for stream in interval["streams"]:
                moved_bytes += stream["bytes"]
                seconds += stream["seconds"]

            seconds /= len(interval["streams"])

            start = int(interval["streams"][0]["start"])
            stats["interval"].append(start)
            stats["port"].append(instance["port"])
            stats["system"].append(system)
            stats["bytes"].append(moved_bytes)
            stats["seconds"].append(seconds)
            stats["direction"].append(direction)


@lru_cache(maxsize=1)
def nc_command(settings: Settings) -> RemoteCommand:
    path = nix_build("netcat-native")
    return settings.remote_command(path)


class Benchmark():
    def __init__(self, settings: Settings):
        self.settings = settings
        self.network = Network(settings)
        self.parallel_iperf = self.settings.remote_command(nix_build("parallel-iperf"))
        self.iperf_client = self.settings.remote_command(nix_build("iperf-client"))

    def _run(
            self,
            local_iperf: str,
            direction: str,
            system: str,
            stats: Dict[str, List[int]],
            extra_env: Dict[str, str] = {}) -> None:
        env = extra_env.copy()
        env.update(flamegraph_env(f"iperf-{direction}-{system}-{NOW}"))
        iperf = f"{self.iperf_client.nix_path}/bin/iperf3"
        fast_ssl = dict(OPENSSL_ia32cap="0x5640020247880000:0x40128")
        env.update(fast_ssl)
        with spawn(local_iperf, "bin/iperf3", "1", extra_env=env) as iperf_server:
            for i in range(60):
                try:
                    nc_command(self.settings).run(
                        "bin/nc", ["-w1", "-z", "-v", self.settings.local_dpdk_ip, "5201"]
                    )
                    break
                except subprocess.CalledProcessError:
                    pass
                status = iperf_server.poll()
                if status is not None:
                    raise OSError(f"iperf exiteded with {status}")
                time.sleep(1)
                if i == 59:
                    raise OSError(f"Could not connect to iperf after 1 min")

            iperf_args = ["client", "-c", self.settings.local_dpdk_ip, "--json", "-t", "10"]
            if direction == "send":
                iperf_args += ["-R"]

            parallel_iperf = self.parallel_iperf.run("bin/parallel-iperf", ["1", iperf] + iperf_args, extra_env=fast_ssl)
            _postprocess_iperf(json.loads(parallel_iperf.stdout), direction, system, stats)
            iperf_server.send_signal(signal.SIGINT)
            try:
                print("wait for iperf to finish...")
                iperf_server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                iperf_server.send_signal(signal.SIGKILL)
                iperf_server.wait()

    def run(self,
            attr: str,
            system: str,
            stats: Dict[str, List[int]],
            extra_env: Dict[str, str] = {}) -> None:
        local_iperf = nix_build(attr)

        self._run(local_iperf, "send", system, stats, extra_env)
        if system == "sgx-io":  # give sgx-lkl-userpci time to shutdown
            import time
            time.sleep(5)
        self._run(local_iperf, "receive", system, stats, extra_env)


def benchmark_native(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.NATIVE)
    benchmark.run("iperf-native", "native", stats, extra_env=extra_env)


def benchmark_scone(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.NATIVE)
    benchmark.run("iperf-scone", "scone", stats, extra_env=extra_env)


def benchmark_sgx_lkl(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.TAP)
    benchmark.run("iperf-sgx-lkl", "sgx-lkl", stats, extra_env=extra_env)


def benchmark_sgx_io(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.DPDK)
    benchmark.run("iperf-sgx-io", "sgx-io", stats, extra_env=extra_env)


def main() -> None:
    stats = read_stats("iperf.json")
    settings = create_settings()
    setup_remote_network(settings)

    benchmark = Benchmark(settings)
    benchmarks = {
        "native": benchmark_native,
        "sgx-io": benchmark_sgx_io,
        "sgx-lkl": benchmark_sgx_lkl,
        "scone": benchmark_scone,
    }

    system = set(stats["system"])
    for name, benchmark_func in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark_func(benchmark, stats)
        write_stats("iperf.json", stats)

    csv = f"iperf-{NOW}.tsv"
    print(csv)
    pd.DataFrame(stats).to_csv(csv, index=False, sep="\t")


if __name__ == "__main__":
    main()
