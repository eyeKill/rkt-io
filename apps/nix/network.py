import getpass
import subprocess
from enum import Enum
from typing import List, Dict

from helpers import ROOT, Settings, nix_build, run


class NetworkKind(Enum):
    NATIVE = 1
    TAP = 2
    DPDK = 3


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

        if kind == NetworkKind.NATIVE:
            ip(["link", "set", self.settings.native_nic_ifname, "up"])

    def extra_env(self, kind: NetworkKind) -> Dict[str, str]:
        if kind == NetworkKind.TAP:
            return dict(
                SGXLKL_IP4=self.settings.local_dpdk_ip,
                SGXLKL_IP6=self.settings.local_dpdk_ip6,
                SGXLKL_TAP_OFFLOAD="1",
                SGXLKL_TAP_MTU="1500",
            )
        elif kind == NetworkKind.DPDK:
            return dict(SGXLKL_DPDK_MTU="1500")
        else:
            return {}

    def setup(self, kind: NetworkKind) -> Dict[str, str]:
        self.bind_driver(kind)

        if kind != NetworkKind.DPDK:
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
        elif kind == NetworkKind.NATIVE:
            for cidr in [self.settings.cidr, self.settings.cidr6]:
                ip(["addr", "add", cidr, "dev", self.settings.native_nic_ifname])

        if kind != NetworkKind.DPDK:
            ip(["link", "set", "dev", self.settings.native_nic_ifname, "mtu", "1500"])
            ip(["link", "set", self.settings.native_nic_ifname, "up"])

        print("########################################")
        return self.extra_env(kind)
