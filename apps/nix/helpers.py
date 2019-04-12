import os
import subprocess
import sys
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List

ROOT = Path(__file__).parent.resolve()


def run(cmd: List[str]) -> subprocess.CompletedProcess:
    print("$ " + " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, check=True)


@contextmanager
def spawn(*args: str, **kwargs) -> Iterator:
    print(f"$ {' '.join(args)}&")
    proc = subprocess.Popen(args, cwd=ROOT)
    try:
        yield proc
    finally:
        print(f"terminate {args[0]}")
        proc.terminate()
        proc.wait(timeout=5)
        proc.kill()


@dataclass
class Settings:
    remote_ssh_host: str
    remote_dpdk_ip: str
    local_dpdk_ip: str
    dpdk_netmask: int
    pci_id: str
    nic_host_driver: str
    nic_host_ifname: str
    nic_dpdk_driver: str

    def run_remote(self, cmd: List[str]) -> subprocess.CompletedProcess:
        cmd = ["ssh", self.remote_ssh_host, "--"] + cmd
        return run(cmd)


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
        nic_host_driver=os.environ.get("NETWORK_HOST_DRIVER", "i40e"),
        nic_host_ifname=os.environ.get("NETWORK_HOST_IFNAME", "eth2"),
        nic_dpdk_driver=os.environ.get("NETWORK_DPDK_DRIVER", "igb_uio")
    )
