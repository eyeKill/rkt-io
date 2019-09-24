import getpass
import os
import subprocess
import tempfile
import time
from enum import Enum
from typing import Any

from helpers import ROOT, Settings, nix_build


class StorageKind(Enum):
    NATIVE = 1
    LKL = 2
    SPDK = 3


def get_total_memory() -> int:
    with open("/proc/meminfo") as f:
        for line in f:
            name, raw_size = line.split(":")
            if name.strip() != "MemTotal":
                continue
            size, unit = raw_size.strip().split()
            return int(size) * 1024
    raise Exception("MemTotal entry not found in /proc/meminfo")


class Mount:
    def __init__(self, kind: StorageKind, dev: str) -> None:
        self.kind = kind
        self.dev = dev

    def __enter__(self) -> str:
        assert self.kind == StorageKind.NATIVE
        self.mountpoint = tempfile.TemporaryDirectory(prefix="iotest-mnt")
        subprocess.run(["sudo", "mount", self.dev, self.mountpoint.name])
        subprocess.run(["sudo", "chown", "-R", getpass.getuser(), self.mountpoint.name])

        return self.mountpoint.name

    def __exit__(self, type: Any, value: Any, traceback: Any) -> None:
        subprocess.run(["sudo", "umount", self.mountpoint.name])

def get_hugepages_num(kind: StorageKind) -> int:
    if kind != StorageKind.SPDK:
        return 0
    total_memory = get_total_memory()
    # leave 5 GB for the system
    gigabyte = 1024 * 1024 * 1024
    spdk_memory = total_memory - 5 * gigabyte
    if spdk_memory < gigabyte:
        raise Exception("Get more memory dude!")

    return int(spdk_memory / 2048 / 1024)

class Storage:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.image = nix_build("iotest-image")


    def setup(self, kind: StorageKind) -> Mount:
        subprocess.run(
            ["sudo", ROOT.joinpath("..", "..", "spdk", "scripts", "setup.sh"), "reset"]
        )
        time.sleep(2)  # wait for device to appear

        spdk_device = self.settings.spdk_device()

        dev = f"/dev/{spdk_device}"

        while not os.path.exists(dev):
            print(".")
            time.sleep(1)

        # TRIM for optimal performance
        subprocess.run(["sudo", "blkdiscard", dev])
        subprocess.run(["sudo", "dd", f"if={self.image}", f"of={dev}"])
        subprocess.run(["sudo", "resize2fs", dev])

        if kind == StorageKind.SPDK:
            subprocess.run(
                [
                    "sudo",
                    ROOT.joinpath("..", "..", "spdk", "scripts", "setup.sh"),
                    "config",
                ]
            )
        elif kind == StorageKind.LKL:
            subprocess.run(["sudo", "chown", getpass.getuser(), dev])

        num_hugepages = get_hugepages_num(kind)
        # spdk setup.sh seems to reset number of pages
        subprocess.run(
            [
                "sudo",
                "sh",
                "-c",
                "echo $0 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages",
                str(num_hugepages),
            ],
            check=True
        )
        # delete existing hugepages to actually free up the memory
        subprocess.run(
            [
                "sudo",
                "find",
                "/dev/hugepages",
                "-name",
                "spdk*map_*",
                "-type",
                "f",
                "-delete",
            ],
            check=True
        )

        return Mount(kind, dev)
