#!/usr/bin/env python3

import hashlib
import os
import subprocess
import sys
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List

if sys.version_info < (3, 7):
    sys.stderr.write("Python 3.7 at least required")
    sys.exit(1)

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

    local_dpdk_ip = os.environ.get("SGXLKL_DPKD_IP4", "10.0.2.1")

    return Settings(
        remote_ssh_host=remote_ssh_host,
        remote_dpdk_ip=remote_dpdk_ip,
        local_dpdk_ip=local_dpdk_ip,
    )


def test_nginx(settings: Settings) -> None:
    nginx = nix_build("nginx")
    with spawn(nginx.strip()):
        for _ in range(10):
            try:
                cmd = ["curl", "-s", settings.local_dpdk_ip + "/test/file-3mb"]
                proc = settings.run_remote(cmd)
                sha256 = hashlib.sha256(proc.stdout).hexdigest()
                expected = (
                    "259da4e49b1d0932c5a16a9809113cf3ea6c7292e827298827e020aa7361f98d"
                )
                assert sha256 == expected, f"{hash} == {expected}"
                break
            except subprocess.CalledProcessError:
                pass


def test_iperf(settings: Settings) -> None:
    nginx = nix_build("iperf")
    with spawn(nginx.strip()):
        for _ in range(10):
            try:
                cmd = ["iperf3", "-c", settings.local_dpdk_ip]
                proc = settings.run_remote(cmd)
                cmd = ["iperf3", "-R", "-c", settings.local_dpdk_ip]
                proc = settings.run_remote(cmd)
                break
            except subprocess.CalledProcessError:
                pass


def main() -> None:
    settings = create_settings()
    test_nginx(settings)
    test_iperf(settings)


if __name__ == "__main__":
    main()
