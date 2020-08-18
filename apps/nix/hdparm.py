#!/usr/bin/env python3
import json
import os
import getpass
import subprocess
import tempfile
import time
from collections import defaultdict
from typing import Any, DefaultDict, Dict, List, Union

from helpers import (
    NOW,
    ROOT,
    Chdir,
    Settings,
    create_settings,
    nix_build,
    run,
    spawn,
    flamegraph_env,
)
from storage import Storage, StorageKind


def benchmark_hdparm(
    storage: Storage,
    system: str,
    attr: str,
    device: str,
    stats: Dict[str, List],
    extra_env: Dict[str, str] = {},
) -> None:
    env = os.environ.copy()
    env.update(flamegraph_env(f"hdparm-{system}-{NOW}"))
    env.update(extra_env)
    hdparm = nix_build(attr)
    print(f"###### {system} >> ######")
    subprocess.run([hdparm, "bin/hdparm", "-Tt", device], env=env)
    print(f"###### {system} << ######")


def benchmark_native(storage: Storage, stats: Dict[str, List]) -> None:
    mnt = storage.setup(StorageKind.NATIVE)
    subprocess.run(["sudo", "chown", getpass.getuser(), mnt.dev])
    benchmark_hdparm(storage, "native", "hdparm-native", mnt.dev, stats)


def benchmark_sgx_lkl(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.LKL)
    benchmark_hdparm(
        storage,
        "sgx-lkl",
        "hdparm",
        "/dev/vdb",
        stats,
        extra_env=dict(SGXLKL_HDS="/dev/nvme0n1:/mnt/nvme"),
    )


def benchmark_sgx_io(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.SPDK)
    benchmark_hdparm(storage, "sgx-io", "hdparm", "/dev/spdk0", stats)


def main() -> None:
    stats: DefaultDict[str, List] = defaultdict(list)

    settings = create_settings()

    storage = Storage(settings)

    benchmark_sgx_io(storage, stats)
    # benchmark_sgx_lkl(storage, stats)
    # benchmark_native(storage, stats)


if __name__ == "__main__":
    main()
