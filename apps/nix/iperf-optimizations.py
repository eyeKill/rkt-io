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

from iperf import _postprocess_iperf, Benchmark


def run_variant(name: str, benchmark: Benchmark, extra_env: Dict[str, str]) -> None:
    stats_file = f"iperf-{name}.json"
    stats = read_stats(stats_file)
    system = set(stats["system"])
    if "sgx-io" in system:
        print(f"skip {name} benchmark")
        return
    else:
        extra_env.update(benchmark.network.setup(NetworkKind.DPDK))
        benchmark.run("iperf-sgx-io", "sgx-io", stats, extra_env=extra_env)
        write_stats(stats_file, stats)

    csv = f"iperf-{name}.tsv"
    print(csv)
    pd.DataFrame(stats).to_csv(csv, index=False, sep="\t")


def main() -> None:
    settings = create_settings()
    setup_remote_network(settings)
    benchmark = Benchmark(settings)
    run_variant("all-on", benchmark, extra_env={})
    run_variant("offload_off", benchmark, extra_env=dict(SGXLKL_GSO_OFFLOAD="0", SGXLKL_CHKSUM_OFFLOAD="0"))
    run_variant("zerocopy_off", benchmark, extra_env=dict(SGXLKL_DPDK_ZEROCOPY="0"))

if __name__ == "__main__":
    main()
