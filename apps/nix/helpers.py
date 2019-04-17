import os
import subprocess
import sys
import signal
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterator, List

ROOT = Path(__file__).parent.resolve()


def run(cmd: List[str], extra_env: Dict[str, str] = {}) -> subprocess.CompletedProcess:
    print("$ " + " ".join(cmd))
    env = os.environ.copy()
    env.update(extra_env)
    return subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, check=True, env=env)


@contextmanager
def spawn(*args: str, **kwargs) -> Iterator:
    print(f"$ {' '.join(args)}&")
    env = os.environ.copy()

    extra_env = kwargs.pop("extra_env", None)
    if extra_env is not None:
        env.update(extra_env)
    proc = subprocess.Popen(args, cwd=ROOT, env=env)

    try:
        yield proc
    finally:
        print(f"terminate {args[0]}")
        proc.send_signal(signal.SIGINT)
        proc.wait()


@dataclass
class Settings:
    remote_ssh_host: str
    remote_dpdk_ip: str
    local_dpdk_ip: str
    dpdk_netmask: int
    pci_id: str
    native_nic_driver: str
    native_nic_ifname: str
    dpdk_nic_driver: str
    tap_ifname: str
    tap_bridge_cidr: str

    @property
    def cidr(self) -> str:
        return f"{self.local_dpdk_ip}/{self.dpdk_netmask}"

    def run_remote(self, cmd: List[str], extra_env: Dict[str, str] = {}) -> subprocess.CompletedProcess:
        cmd = ["ssh", self.remote_ssh_host, "--"] + cmd
        return run(cmd, extra_env=extra_env)


def nix_build(attr: str) -> str:
    return run(["nix-build", "-A", str(attr)]).stdout.decode("utf-8")


def create_settings() -> Settings:
    remote_ssh_host = os.environ.get("REMOTE_SSH_HOST", None)
    if not remote_ssh_host:
        print("REMOTE_SSH_HOST not set", file=sys.stderr)
        sys.exit(1)

    remote_dpdk_ip = os.environ.get("REMOTE_DPDK_IP", "10.0.2.2")
    if not remote_dpdk_ip:
        print("REMOTE_DPDK_IP not set", file=sys.stderr)
        sys.exit(1)

    pci_id = os.environ.get("NETWORK_PCI_DEV_ID")
    if not pci_id:
        print("NETWORK_PCI_DEV_ID not set", file=sys.stderr)
        sys.exit(1)
    return Settings(
        remote_ssh_host=remote_ssh_host,
        remote_dpdk_ip=remote_dpdk_ip,
        local_dpdk_ip=os.environ.get("SGXLKL_DPKD_IP4", "10.0.2.1"),
        dpdk_netmask=int(os.environ.get("DEFAULT_DPDK_IPV4_MASK", "24")),
        pci_id=pci_id,
        native_nic_driver=os.environ.get("NATIVE_NETWORK_DRIVER", "i40e"),
        native_nic_ifname=os.environ.get("NATIVE_NETWORK_IFNAME", "eth2"),
        dpdk_nic_driver=os.environ.get("DPDK_NETWORK_DRIVER", "igb_uio"),
        tap_ifname=os.environ.get("SGXLKL_TAP", "sgxlkl_tap0"),
        tap_bridge_cidr=os.environ.get("SGXLKL_BRIDGE_CIDR", "10.0.2.3/24"),
    )
