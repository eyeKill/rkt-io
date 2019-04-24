import getpass
import json
import os
import subprocess
import tempfile
import time
from collections import defaultdict
from enum import Enum
from typing import Any, DefaultDict, Dict, List, Union

import pandas as pd
from helpers import (NOW, ROOT, Chdir, Settings, create_settings, nix_build,
                     run, spawn)


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


def percentile(idx: int, run_total: List[int]) -> float:
    total = run_total[len(run_total) - 1]
    if total == 0:
        return 0

    return float(run_total[idx]) / total


def more_lines(indices: Dict[str, int], bins: Dict[str, List[List[Any]]]) -> bool:
    for key, value in indices.items():
        if value < len(bins[key]):
            return True

    return False


def parse_json_plus(
    jsondata: Dict[str, Any], system: str, stats: Dict[str, List]
) -> None:
    for jobnum in range(0, len(jsondata["jobs"])):
        bins = {}
        run_total = {}
        operation_set = set(["read", "write", "trim"])

        prev_operation = None
        for operation in operation_set:
            if "bins" in jsondata["jobs"][jobnum][operation]["clat_ns"]:
                bins_loc = "clat_ns"
            elif "bins" in jsondata["jobs"][jobnum][operation]["lat_ns"]:
                bins_loc = "lat_ns"
            else:
                raise RuntimeError(
                    "Latency bins not found. "
                    "Are you sure you are using json+ output?"
                )

            bin = []
            for key, value in jsondata["jobs"][jobnum][operation][bins_loc][
                "bins"
            ].items():
                bin.append([int(key), value])
            bins[operation] = bin
            bins[operation] = sorted(bins[operation], key=lambda bin: bin[0])

            run_total[operation] = [0 for x in range(0, len(bins[operation]))]
            if len(bins[operation]) > 0:
                run_total[operation][0] = bins[operation][0][1]
                for x in range(1, len(bins[operation])):
                    run_total[operation][x] = (
                        run_total[operation][x - 1] + bins[operation][x][1]
                    )

        # Have a counter for each operation
        # In each round, pick the shortest remaining duration
        # and output a line with any values for that duration
        indices = {x: 0 for x in operation_set}
        while more_lines(indices, bins):
            min_lat = 17_112_760_320
            for operation in operation_set:
                if indices[operation] < len(bins[operation]):
                    min_lat = min(bins[operation][indices[operation]][0], min_lat)

            stats["job"].append(jobnum)
            stats["system"].append(system)
            stats[f"{bins_loc}ec"].append(min_lat)

            for operation in operation_set:
                if (
                    indices[operation] < len(bins[operation])
                    and min_lat == bins[operation][indices[operation]][0]
                ):
                    count = bins[operation][indices[operation]][1]
                    cumulative = run_total[operation][indices[operation]]
                    ptile = percentile(indices[operation], run_total[operation])
                    stats[f"{operation}_count"].append(count)
                    stats[f"{operation}_cumulative"].append(cumulative)
                    stats[f"{operation}_percentile"].append(ptile)
                    indices[operation] += 1
                else:
                    stats[f"{operation}_count"].append(None)
                    stats[f"{operation}_cumulative"].append(None)
                    stats[f"{operation}_percentile"].append(None)


def benchmark_fio(
    storage: Storage, system: str, attr: str, directory: str, stats: Dict[str, List], extra_env: Dict[str,str]={}
):
    flamegraph = f"fio-{NOW}.svg"
    print(flamegraph)

    env = dict(
        FLAMEGRAPH_FILENAME=flamegraph,
        SGXLKL_ENABLE_FLAMEGRAPH="1",
        SGXLKL_CWD=directory,
    )
    env.update(extra_env)
    fio = nix_build(attr)
    proc = run([fio], extra_env=env)
    jsondata = json.loads(proc.stdout)
    parse_json_plus(jsondata, system, stats)


def benchmark_native(storage: Storage, stats: Dict[str, List]) -> None:
    with storage.setup(StorageKind.NATIVE) as mnt:
        benchmark_fio(storage, "native", "fio-native", mnt, stats)


def benchmark_sgx_lkl(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.LKL)
    benchmark_fio(storage, "sgx-lkl", "fio", "/mnt/nvme", stats, extra_env=dict(SGXLKL_HDS="/dev/nvme0n1:/mnt/nvme"))


def benchmark_sgx_io(storage: Storage, stats: Dict[str, List]) -> None:
    storage.setup(StorageKind.SPDK)
    benchmark_fio(storage, "sgx-io", "fio", "/mnt/vdb", stats)


def main() -> None:
    stats: DefaultDict[str, List] = defaultdict(list)

    settings = create_settings()

    storage = Storage(settings)

    benchmark_native(storage, stats)
    benchmark_sgx_lkl(storage, stats)
    benchmark_sgx_io(storage, stats)

    csv = f"fio-{NOW}.tsv"
    print(csv)
    pd.DataFrame(stats).to_csv(csv, index=False)


if __name__ == "__main__":
    main()
