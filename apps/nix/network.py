import getpass
import subprocess
from enum import Enum
from typing import List, Dict

from helpers import ROOT, Settings, nix_build, run
from storage import setup_hugepages, StorageKind


class NetworkKind(Enum):
    NATIVE = 1
    CLIENT_NATIVE = 2
    TAP = 3
    DPDK = 4


def ip(args: List[str]) -> None:
    run(["sudo", "ip"] + args)


def remote_cmd(ssh_host: str, args: List[str]) -> None:
    run(["ssh", ssh_host, "--"] + args)


def setup_remote_network(settings: Settings) -> None:
    cmds = [
        ["sudo", "ip", "link", "set", settings.remote_nic_ifname, "up"],
        ["sudo", "ip", "addr", "flush", "dev", settings.remote_nic_ifname],
        ["sudo", "ip", "addr", "add", f"{settings.remote_dpdk_ip}/{settings.dpdk_netmask}", "dev", settings.remote_nic_ifname],
        ["sudo", "ip", "link", "set", settings.remote_nic_ifname, "mtu", "1500"],
        ["sudo", "ip", "link", "set", settings.remote_nic_ifname, "up"]
    ]
    command = "; ".join(map(lambda cmd: " ".join(cmd), cmds))
    remote_cmd(settings.remote_ssh_host, [command])


class Network:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    def bind_driver(self, kind: NetworkKind) -> None:
        devbind = ROOT.joinpath("..", "..", "dpdk", "usertools", "dpdk-devbind.py")

        if kind != NetworkKind.DPDK:
            driver = self.settings.native_nic_driver
        else:
            driver = self.settings.dpdk_nic_driver
            try:
                ip(["link", "set", self.settings.native_nic_ifname, "down"])
            except subprocess.CalledProcessError:  # interface may not exists
                pass

        run(["sudo", "python3", str(devbind), "-b", driver, self.settings.nic_pci_id])

        if kind == NetworkKind.NATIVE or kind == NetworkKind.CLIENT_NATIVE:
            ip(["link", "set", self.settings.native_nic_ifname, "up"])

    def extra_env(self, kind: NetworkKind) -> Dict[str, str]:
        if kind == NetworkKind.TAP:
            return dict(
                SGXLKL_IP4=self.settings.local_dpdk_ip,
                SGXLKL_GW4="",
                SGXLKL_IP6=self.settings.local_dpdk_ip6,
                SGXLKL_TAP_OFFLOAD="1",
                SGXLKL_TAP_MTU="1500",
            )
        elif kind == NetworkKind.DPDK:
            sysctl = "net.core.rmem_max=56623104;net.core.wmem_max=56623104;net.core.rmem_default=56623104;net.core.wmem_default=56623104;net.core.optmem_max=40960;net.ipv4.tcp_rmem=4096 87380 56623104;net.ipv4.tcp_wmem=4096 65536 56623104;net.core.somaxconn=1024;net.core.netdev_max_backlog=2000"
            return dict(SGXLKL_DPDK_MTU="1500", SGXLKL_SYSCTL=sysctl)
        else:
            return {}

    def setup(self, kind: NetworkKind) -> Dict[str, str]:
        self.bind_driver(kind)

        if kind == NetworkKind.DPDK:
            setup_hugepages(StorageKind.SPDK)
        else:
            setup_hugepages(StorageKind.NATIVE)
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
        ip(["link", "set", "dev", self.settings.tap_ifname, "mtu", "1500"])
        ip(["link", "set", "dev", self.settings.tap_ifname, "up"])

        subprocess.run(["sudo", "ip", "link", "del", "iperf-br"])

        if kind == NetworkKind.TAP:
            ip(["link", "add", "name", "iperf-br", "mtu", "1500", "type", "bridge"])
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
        elif kind == NetworkKind.NATIVE or kind == NetworkKind.CLIENT_NATIVE:
            if kind == NetworkKind.CLIENT_NATIVE:
                cidrs = [self.settings.remote_cidr, self.settings.remote_cidr6]
            else:
                cidrs = [self.settings.cidr, self.settings.cidr6]
            for cidr in cidrs:
                ip(["addr", "add", cidr, "dev", self.settings.native_nic_ifname])

        if kind != NetworkKind.DPDK:
            ip(["link", "set", "dev", self.settings.native_nic_ifname, "mtu", "1500"])
            ip(["link", "set", self.settings.native_nic_ifname, "up"])

        print("########################################")
        return self.extra_env(kind)
