import os
import json
import json
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
        env.update(SGXLKL_ETHREADS="1")
        simpleio = nix_build(attribute)

        cmd = [str(simpleio), "bin/udp-send", self.settings.remote_dpdk_ip, "1000000"]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, env=env)
        assert proc.stdout is not None
        for line in proc.stdout:
            print(line)
            if '"time": ' in line:
                data = json.loads(line)
                stats["system"].append(system)
                stats["time"].append(data["time"])
                return
        proc.terminate()
        proc.wait()

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
    # not working yet
    #"sync": benchmark_sync,
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
