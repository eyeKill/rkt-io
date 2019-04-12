#!/usr/bin/env python3

import json
import subprocess

from helpers import ROOT, Settings, create_settings, nix_build, run, spawn


def run_iperf(settings: Settings, reverse: bool = False):
    cmd = ["iperf3"]
    if reverse:
        cmd += ["-R"]
    cmd += ["-c", settings.local_dpdk_ip, "--json"]

    proc = settings.run_remote(cmd)
    return json.loads(proc.stdout)


def map_nic_driver(settings: Settings, host: bool = True):
    devbind = ROOT.joinpath("..", "..", "dpdk", "usertools", "dpdk-devbind.py")
    driver = settings.nic_host_driver if host else settings.nic_dpdk_driver

    run(["sudo", str(devbind), "-b", driver, settings.pci_id])


def benchmark_iperf(settings: Settings, attr: str) -> None:
    iperf = nix_build(attr)
    with spawn(iperf.strip()):
        while True:
            cmd = ["iperf3", "-c", settings.local_dpdk_ip, "-n", "1024"]
            proc = settings.run_remote(cmd)
            if proc.returncode == 0:
                break
        stats = run_iperf(settings)
        stats_reverse = run_iperf(settings, reverse=True)


def main() -> None:
    settings = create_settings()
    map_nic_driver(settings, host=True)
    run(["sudo", "ip", "addr", "flush", "dev", settings.nic_host_ifname])
    cidr = f"{settings.local_dpdk_ip}/{settings.dpdk_netmask}"
    run(["sudo", "ip", "addr", "add", cidr, "dev", settings.nic_host_ifname])
    benchmark_iperf(settings, "iperf-host")

    run(["sudo", "ip", "link", "set", settings.nic_host_ifname, "down"])
    map_nic_driver(settings, host=False)
    benchmark_iperf(settings, "iperf")


if __name__ == "__main__":
    main()
