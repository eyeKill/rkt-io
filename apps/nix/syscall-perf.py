import os
import json
from typing import Dict, List
import subprocess
import signal
import pandas as pd

from helpers import (
    NOW,
    create_settings,
    nix_build,
    read_stats,
    write_stats,
)
from network import Network, NetworkKind


class Benchmark:
    def __init__(self) -> None:
        self.settings = create_settings()
        self.network = Network(self.settings)

    def run(
        self,
        attribute: str,
        system: str,
        stats: Dict[str, List],
        extra_env: Dict[str, str] = {},
    ) -> None:
        env = os.environ.copy()
        env.update(extra_env)
        env["SGXLKL_ETHREADS"] = "2" if system == "sync" else "1"
        #env["SGXLKL_ETHREADS"] = "1" if system == "direct" else "8"
        simpleio = nix_build(attribute)

        cmd = [str(simpleio), "bin/udp-send", self.settings.remote_dpdk_ip, "2000000"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, env=env)
        assert proc.stdout is not None
        found_results = False
        for line in proc.stdout:
            line = line.rstrip()
            print(line)
            if found_results:
                if line == "</results>":
                    break
                data = json.loads(line)
                stats["system"].append(system)
                for k, v in data.items():
                    stats[k].append(v)
            elif line == "<results>":
                found_results = True
        proc.send_signal(signal.SIGINT)
        proc.wait()

        if not found_results:
            raise Exception("no time found in results")


def benchmark_native(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.NATIVE)
    benchmark.run(
        "simpleio-native", "native", stats, extra_env=extra_env
    )


def benchmark_async(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.TAP)
    benchmark.run(
        "simpleio-sgx-lkl", "async", stats, extra_env=extra_env
    )


def benchmark_sync(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.TAP)
    extra_env["SGXLKL_EXIT_ON_HOST_CALLS"] = "1"
    extra_env["SGXLKL_WAIT_ON_HOST_CALLS"] = "1"
    benchmark.run(
        "simpleio-sgx-lkl", "sync", stats, extra_env=extra_env
    )


def benchmark_direct(benchmark: Benchmark, stats: Dict[str, List[int]]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.DPDK)
    benchmark.run("simpleio-sgx-io", "direct", stats, extra_env=extra_env)


BENCHMARKS = {
    "native": benchmark_native,
    "async": benchmark_async,
    "sync": benchmark_sync,
    "direct": benchmark_direct,
}


def main() -> None:
    stats = read_stats("syscall-perf.json")
    system = set(stats["system"])
    benchmark = Benchmark()

    for name, benchmark_func in BENCHMARKS.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        benchmark_func(benchmark, stats)
        write_stats("syscall-perf.json", stats)

    csv = f"syscall-perf-{NOW}.tsv"
    print(csv)
    df = pd.DataFrame(stats)
    df.to_csv(csv, index=False, sep="\t")
    df.to_csv("syscall-perf-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
