import os
import signal
import subprocess
import sys
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterator, List

ROOT = Path(__file__).parent.resolve()
NOW = datetime.now().strftime("%Y%m%d-%H%M%S")


def run(cmd: List[str], extra_env: Dict[str, str] = {}) -> subprocess.CompletedProcess:
    print("$ " + " ".join(cmd))
    env = os.environ.copy()
    env.update(extra_env)
    return subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, check=True, env=env)


class Chdir(object):
    def __init__(self, path):
        self.old_dir = os.getcwd()
        self.new_dir = path

    def __enter__(self):
        os.chdir(self.new_dir)

    def __exit__(self, *args):
        os.chdir(self.old_dir)


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
    nvme_pci_id: str
    nic_pci_id: str
    native_nic_driver: str
    native_nic_ifname: str
    dpdk_nic_driver: str
    tap_ifname: str
    tap_bridge_cidr: str

    @property
    def cidr(self) -> str:
        return f"{self.local_dpdk_ip}/{self.dpdk_netmask}"

    def run_remote(
        self, cmd: List[str], extra_env: Dict[str, str] = {}
    ) -> subprocess.CompletedProcess:
        cmd = ["ssh", self.remote_ssh_host, "--"] + cmd
        return run(cmd, extra_env=extra_env)

    def spdk_device(self) -> str:
        for device in os.listdir("/sys/block"):
            path = Path(f"/sys/block/{device}").resolve()
            if not str(path).startswith("/sys/devices/pci"):
                continue
            pci_id = path.parents[2].name
            if pci_id == self.nvme_pci_id:
                return device
        raise Exception(f"No block device with PCI ID {self.nvme_pci_id} found")


def nix_build(attr: str) -> str:
    return run(["nix-build", "-A", str(attr)]).stdout.decode("utf-8").strip()


def create_settings() -> Settings:
    remote_ssh_host = os.environ.get("REMOTE_SSH_HOST", None)
    if not remote_ssh_host:
        print("REMOTE_SSH_HOST not set", file=sys.stderr)
        sys.exit(1)

    remote_dpdk_ip = os.environ.get("REMOTE_DPDK_IP", "10.0.2.2")
    if not remote_dpdk_ip:
        print("REMOTE_DPDK_IP not set", file=sys.stderr)
        sys.exit(1)

    nic_pci_id = os.environ.get("NIC_PCI_ID")
    if not nic_pci_id:
        print("NIC_PCI_ID not set", file=sys.stderr)
        sys.exit(1)

    nvme_pci_id = os.environ.get("NVME_PCI_ID")
    if not nvme_pci_id:
        print("NVME_PCI_ID not set", file=sys.stderr)
        sys.exit(1)

    return Settings(
        remote_ssh_host=remote_ssh_host,
        remote_dpdk_ip=remote_dpdk_ip,
        local_dpdk_ip=os.environ.get("SGXLKL_DPKD_IP4", "10.0.2.1"),
        dpdk_netmask=int(os.environ.get("DEFAULT_DPDK_IPV4_MASK", "24")),
        nic_pci_id=nic_pci_id,
        nvme_pci_id=nvme_pci_id,
        native_nic_driver=os.environ.get("NATIVE_NETWORK_DRIVER", "i40e"),
        native_nic_ifname=os.environ.get("NATIVE_NETWORK_IFNAME", "eth2"),
        dpdk_nic_driver=os.environ.get("DPDK_NETWORK_DRIVER", "igb_uio"),
        tap_ifname=os.environ.get("SGXLKL_TAP", "sgxlkl_tap0"),
        tap_bridge_cidr=os.environ.get("SGXLKL_BRIDGE_CIDR", "10.0.2.3/24"),
    )
