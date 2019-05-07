import getpass
import subprocess
from enum import Enum
from typing import List

from helpers import ROOT, Settings, nix_build, run


class NetworkKind(Enum):
    NATIVE = 1
    TAP = 2
    DPDK = 3


def ip(args: List[str]) -> None:
    run(["sudo", "ip"] + args)


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

        run(["sudo", str(devbind), "-b", driver, self.settings.nic_pci_id])

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

        ip(
            [
                "tuntap",
                "add",
                "dev",
                self.settings.tap_ifname,
                "mode",
                "tap",
                "user",
                getpass.getuser(),
            ]
        )
        ip(["link", "set", "dev", self.settings.tap_ifname, "mtu", "9000"])
        ip(["link", "set", "dev", self.settings.tap_ifname, "up"])

        subprocess.run(["sudo", "ip", "link", "del", "iperf-br"])

        if self.kind == NetworkKind.TAP:
            ip(["link", "add", "name", "iperf-br", "mtu", "9000", "type", "bridge"])
            ip(["link", "set", "dev", "iperf-br", "up"])
            ip(["link", "set", self.settings.native_nic_ifname, "master", "iperf-br"])
            ip(["link", "set", self.settings.tap_ifname, "master", "iperf-br"])
            run(
                [
                    "sudo",
                    "sysctl",
                    "-w",
                    "net.ipv4.ip_forward=1",
                    "net.ipv6.conf.all.forwarding=1",
                ]
            )
            for cidr in [self.settings.tap_bridge_cidr, self.settings.tap_bridge_cidr6]:
                ip(["addr", "add", "dev", "iperf-br", cidr])
        elif self.kind == NetworkKind.NATIVE:
            for cidr in [self.settings.cidr, self.settings.cidr6]:
                ip(["addr", "add", cidr, "dev", self.settings.native_nic_ifname])

        if self.kind != NetworkKind.DPDK:
            ip(["link", "set", "dev", self.settings.native_nic_ifname, "mtu", "9000"])
            ip(["link", "set", self.settings.native_nic_ifname, "up"])
