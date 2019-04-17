#!/usr/bin/env python3

import getpass
import json
import os
import subprocess
import time
from collections import defaultdict
from datetime import datetime
from enum import Enum
from typing import Any, DefaultDict, Dict, List

import pandas as pd
from helpers import ROOT, Settings, create_settings, nix_build, run, spawn

NOW = datetime.now().strftime("%Y%m%d-%H%M%S")


def run_iperf(settings: Settings, send: bool = False, extra_env: Dict[str, str] = {}):
    cmd = ["iperf3"]
    if send:
        cmd += ["-R"]
    cmd += ["-c", settings.local_dpdk_ip, "--json"]

    proc = settings.run_remote(cmd, extra_env=extra_env)
    return json.loads(proc.stdout)


def ip(args: List[str]) -> None:
    run(["sudo", "ip"] + args)


class NetworkKind(Enum):
    NATIVE = 1
    BRIDGE = 2
    DPDK = 3


class Network:
    def __init__(self, kind: NetworkKind, settings: Settings) -> None:
        self.kind = kind
        self.settings = settings

    def bind_driver(self) -> None:
        devbind = ROOT.joinpath("..", "..", "dpdk", "usertools", "dpdk-devbind.py")

        if self.kind != NetworkKind.DPDK:
            driver = self.settings.native_nic_driver
        else:
            driver = self.settings.dpdk_nic_driver
            try:
                ip(["link", "set", self.settings.native_nic_ifname, "down"])
            except subprocess.CalledProcessError:  # interface may not exists
                pass

        run(["sudo", str(devbind), "-b", driver, self.settings.pci_id])

        if self.kind == NetworkKind.NATIVE:
            ip(["link", "set", self.settings.native_nic_ifname, "up"])

    def setup(self) -> None:
        self.bind_driver()

        if self.kind != NetworkKind.DPDK:
            ip(["addr", "flush", "dev", self.settings.native_nic_ifname])

        try:
            ip(["link", "del", self.settings.tap_ifname])
        except subprocess.CalledProcessError:
            # might not exists
            pass

        ip(["tuntap", "add", "dev", self.settings.tap_ifname, "mode", "tap", "user", getpass.getuser()])
        ip(["link", "set", "dev", self.settings.tap_ifname, "up"])

        subprocess.run(["sudo", "ip", "link", "del", "iperf-br"])

        if self.kind == NetworkKind.BRIDGE:
            ip(["link", "add", "name", "iperf-br", "type", "bridge"])
            ip(["link", "set", "dev", "iperf-br", "up"])
            ip(["link", "set", self.settings.native_nic_ifname, "master", "iperf-br"])
            ip(["link", "set", self.settings.tap_ifname, "master", "iperf-br"])
            run(["sudo", "sysctl", "-w", "net.ipv4.ip_forward=1"])
            ip(["addr", "add", "dev", "iperf-br", self.settings.tap_bridge_cidr])
        elif self.kind == NetworkKind.NATIVE:
            ip(
                [
                    "addr",
                    "add",
                    self.settings.cidr,
                    "dev",
                    self.settings.native_nic_ifname,
                ]
            )

        if self.kind != NetworkKind.DPDK:
            ip(["link", "set", self.settings.native_nic_ifname, "up"])


def _postprocess_iperf(
    raw_data: Dict[str, Any], direction: str, system: str, stats: Dict[str, Any]
) -> None:
    cpu = raw_data["end"]["cpu_utilization_percent"]

    for intervall in raw_data["intervals"]:
        for key in cpu.keys():
            stats[f"cpu_{key}"].append(cpu[key])

        moved_bytes = 0
        seconds = 0
        for stream in intervall["streams"]:
            moved_bytes += stream["bytes"]
            seconds += stream["seconds"]

        stats["system"].append(system)
        stats["bytes"].append(moved_bytes)
        stats["seconds"].append(seconds)
        stats["direction"].append(direction)


def _benchmark_iperf(
    settings: Settings,
    iperf_cmd: str,
    direction: str,
    system: str,
    stats: Dict[str, List[int]],
    extra_env: Dict[str, str] = {},
):
    env = extra_env.copy()
    flamegraph = f"iperf-{direction}-{system}-{NOW}.svg"
    print(flamegraph)
    env.update(FLAMEGRAPH_FILENAME=flamegraph,
               SGXLKL_ENABLE_FLAMEGRAPH="1")
    with spawn(iperf_cmd, extra_env=env):
        while True:
            cmd = ["iperf3", "-c", settings.local_dpdk_ip, "-n", "1024"]
            try:
                proc = settings.run_remote(cmd)
                break
            except subprocess.CalledProcessError:
                print(".")
                pass

        _postprocess_iperf(run_iperf(settings), direction, system, stats)

    while not os.path.exists(flamegraph):
        print(".")
        time.sleep(1)


def benchmark_iperf(
    settings: Settings,
    attr: str,
    system: str,
    stats: Dict[str, List[int]],
    extra_env: Dict[str, str] = {},
) -> None:
    iperf = nix_build(attr).strip()

    _benchmark_iperf(settings, iperf, "send", system, stats, extra_env)
    _benchmark_iperf(settings, iperf, "receive", system, stats, extra_env)


def benchmark_native(settings: Settings, stats: Dict[str, List[int]]):
    Network(NetworkKind.NATIVE, settings).setup()

    benchmark_iperf(settings, "iperf-host", "native", stats)


def benchmark_sgx_lkl(settings: Settings, stats: Dict[str, List[int]]):
    Network(NetworkKind.BRIDGE, settings).setup()
    extra_env = dict(SGXLKL_IP4=settings.local_dpdk_ip)
    benchmark_iperf(settings, "iperf", "sgx-lkl", stats, extra_env=extra_env)


def benchmark_sgx_io(settings: Settings, stats: Dict[str, List[int]]):
    Network(NetworkKind.DPDK, settings).setup()
    benchmark_iperf(settings, "iperf", "sgx-io", stats)


def main() -> None:
    stats: DefaultDict[str, List] = defaultdict(list)

    settings = create_settings()

    benchmark_native(settings, stats)
    benchmark_sgx_lkl(settings, stats)
    benchmark_sgx_io(settings, stats)

    csv = f"iperf-{NOW}.tsv"
    print(csv)
    pd.DataFrame(stats).to_csv(csv, index=False)


if __name__ == "__main__":
    main()
