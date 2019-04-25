import json
import os
import subprocess
import tempfile
import time
from collections import defaultdict
from enum import Enum
from functools import lru_cache
from typing import Any, DefaultDict, Dict, List, Union

import pandas as pd
from helpers import (
    NOW,
    ROOT,
    Chdir,
    RemoteCommand,
    Settings,
    create_settings,
    nix_build,
    run,
    spawn,
)
from network import Network, NetworkKind
from storage import Storage, StorageKind


@lru_cache(maxsize=1)
def sysbench_command(settings: Settings) -> RemoteCommand:
    path = nix_build("sysbench")
    return settings.remote_command(path)


@lru_cache(maxsize=1)
def nc_command(settings: Settings) -> RemoteCommand:
    path = nix_build("netcat")
    return settings.remote_command(path)


def benchmark_mysql(
    storage: Storage,
    attr: str,
    system: str,
    mnt: str,
    stats: Dict[str, List[int]],
    extra_env: Dict[str, str] = {},
) -> None:
    flamegraph = f"sysbench-{NOW}.svg"
    print(flamegraph)

    env = dict(
        FLAMEGRAPH_FILENAME=flamegraph, SGXLKL_ENABLE_FLAMEGRAPH="1", SGXLKL_CWD=mnt
    )
    env.update(extra_env)
    mysql = nix_build(attr)
    sysbench = sysbench_command(storage.settings)

    with spawn(
        mysql,
        "bin/mysqld",
        f"--datadir={mnt}/var/lib/mysql",
        "--socket=/tmp/mysql.sock",
        extra_env=env,
    ):
        common_flags = [
            f"--mysql-host={storage.settings.local_dpdk_ip}",
            "--mysql-db=root",
            "--mysql-user=root",
            "--mysql-password=root",
            f"{sysbench.nix_path}/share/sysbench/oltp_read_write.lua",
        ]

        while True:
            try:
                proc = nc_command(storage.settings).run(
                    "bin/nc", ["-z", "-v", storage.settings.local_dpdk_ip, "3306"]
                )
                break
            except subprocess.CalledProcessError:
                print(".")
                pass

        try:
            sysbench.run("bin/sysbench", common_flags + ["prepare"])
        except subprocess.CalledProcessError as f:
            breakpoint()
        sysbench.run("bin/sysbench", common_flags + ["run"])
        sysbench.run("bin/sysbench", common_flags + ["cleanup"])


def benchmark_native(storage: Storage, stats: Dict[str, List[int]]) -> None:
    with storage.setup(StorageKind.NATIVE) as mnt:
        Network(NetworkKind.NATIVE, storage.settings).setup()
        benchmark_mysql(storage, "mariadb-native", "fio-native", mnt, stats)


def benchmark_sgx_lkl(storage: Storage, stats: Dict[str, List[int]]) -> None:
    Network(NetworkKind.BRIDGE, storage.settings).setup()
    storage.setup(StorageKind.LKL)
    extra_env = dict(SGXLKL_IP4=storage.settings.local_dpdk_ip, SGXLKL_HDS="/dev/nvme0n1:/mnt/nvme")
    benchmark_mysql(
        storage,
        "mariadb",
        "sgx-lkl",
        "/mnt/nvme",
        stats,
        extra_env=extra_env,
    )


def benchmark_sgx_io(storage: Storage, stats: Dict[str, List[int]]):
    Network(NetworkKind.DPDK, storage.settings).setup()
    storage.setup(StorageKind.SPDK)
    benchmark_mysql(storage, "mariadb", "sgx-io", "/mnt/vdb", stats)


def main() -> None:
    stats: DefaultDict[str, List] = defaultdict(list)

    settings = create_settings()
    storage = Storage(settings)
    #benchmark_native(storage, stats)
    benchmark_sgx_lkl(storage, stats)
    benchmark_sgx_io(storage, stats)


if __name__ == "__main__":
    main()
