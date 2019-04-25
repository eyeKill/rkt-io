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

        return Mount(kind, dev)
