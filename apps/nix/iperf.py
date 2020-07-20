#!/usr/bin/env python3

import sys
import json
import subprocess
from collections import defaultdict
from typing import Any, DefaultDict, Dict, List

import pandas as pd
from helpers import (
    NOW,
    ROOT,
    RemoteCommand,
    Settings,
    create_settings,
    flamegraph_env,
    nix_build,
    run,
    spawn,
)
from network import Network, NetworkKind


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


def _benchmark_iperf(
    settings: Settings,
    local_iperf: str,
    remote_iperf: RemoteCommand,
    direction: str,
    system: str,
    stats: Dict[str, List[int]],
    extra_env: Dict[str, str] = {},
):
    env = extra_env.copy()
    env.update(flamegraph_env(f"iperf-{direction}-{system}-{NOW}"))
    env[
        "SGXLKL_SYSCTL"
    ] = "net.core.rmem_max=56623104;net.core.wmem_max=56623104;net.core.rmem_default=56623104;net.core.wmem_default=56623104;net.core.optmem_max=40960;net.ipv4.tcp_rmem=4096 87380 56623104;net.ipv4.tcp_wmem=4096 65536 56623104;"
    with spawn(local_iperf, extra_env=env):
        # if True:
        while True:
            try:
                proc = remote_iperf.run(
                    "bin/iperf",
                    [
                        "-c",
                        settings.local_dpdk_ip,
                        "-n",
                        "1024",
                        "--connect-timeout",
                        "3",
                    ],
                )
                break
            except subprocess.CalledProcessError:
                print(".")
                pass

        iperf_args = ["-c", settings.local_dpdk_ip, "--json", "-t", "10"]
        if direction == "send":
            iperf_args += ["-R"]

        proc = remote_iperf.run(
            "bin/parallel-iperf", ["1", "iperf3"] + iperf_args, extra_env=extra_env
        )
        _postprocess_iperf(json.loads(proc.stdout), direction, system, stats)


def benchmark_iperf(
    settings: Settings,
    attr: str,
    system: str,
    stats: Dict[str, List[int]],
    extra_env: Dict[str, str] = {},
) -> None:
    local_iperf = nix_build(attr)
    remote_iperf = settings.remote_command(nix_build("parallel-iperf"))

    _benchmark_iperf(
        settings, local_iperf, remote_iperf, "send", system, stats, extra_env
    )
    _benchmark_iperf(
        settings, local_iperf, remote_iperf, "receive", system, stats, extra_env
    )


def benchmark_native(settings: Settings, stats: Dict[str, List[int]]) -> None:
    Network(NetworkKind.NATIVE, settings).setup()

    benchmark_iperf(settings, "iperf-native", "native", stats)


def benchmark_sgx_lkl(settings: Settings, stats: Dict[str, List[int]]) -> None:
    Network(NetworkKind.TAP, settings).setup()
    extra_env = dict(
        SGXLKL_IP4=settings.local_dpdk_ip,
        SGXLKL_IP6=settings.local_dpdk_ip6,
        SGXLKL_TAP_OFFLOAD="1",
        SGXLKL_TAP_MTU="1500",
    )
    benchmark_iperf(settings, "iperf", "sgx-lkl", stats, extra_env=extra_env)


def benchmark_sgx_io(settings: Settings, stats: Dict[str, List[int]]):
    Network(NetworkKind.DPDK, settings).setup()
    extra_env = dict(SGXLKL_DPDK_MTU="1500")

    benchmark_iperf(settings, "iperf", "sgx-io", stats, extra_env=extra_env)


def main() -> None:
    stats: DefaultDict[str, List] = defaultdict(list)

    settings = create_settings()

    benchmark_sgx_io(settings, stats)
    benchmark_sgx_lkl(settings, stats)
    benchmark_native(settings, stats)

    csv = f"iperf-{NOW}.tsv"
    print(csv)
    pd.DataFrame(stats).to_csv(csv, index=False, sep="\t")


if __name__ == "__main__":
    main()
