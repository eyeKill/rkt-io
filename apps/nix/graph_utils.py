#!/usr/bin/env python3
from typing import Dict, List, Any, Union
import pandas as pd

SYSTEM_ALIASES: Dict[str, str] = {"sgx-io": "rkt-io"}
OPERATION_ALIASES: Dict[str, str] = {
    "read-bw": "read",
    "write-bw": "write",
    "[READ]": "read",
    "[UPDATE]": "update",
}
DPDK_ALIASES: Dict[str, str] = {
    "dpdk-zerocopy": "zerocopy",
    "dpdk-copy": "copy",
    "dpdk-offload": "offload",
    "dpdk-no-offload": "no offload",
}
SPDK_ALIASES: Dict[str, str] = {
    "spdk-zerocopy": "zerocopy",
    "spdk-copy": "copy",
    "Timing buffer-cache reads": "cache",
    "Timing buffered disk reads": "buffer",
}
HDPARM_ALIASES: Dict[str, str] = {
    "Timing buffer-cache reads": "cache",
    "Timing buffered disk reads": "buffer",
}
ROW_ALIASES = dict(
    system=SYSTEM_ALIASES,
    operation=OPERATION_ALIASES,
    feature_dpdk=DPDK_ALIASES,
    feature_spdk=SPDK_ALIASES,
    hdparm_kind=HDPARM_ALIASES,
    type={
        "x86_acc": "aes-ni",
        "no_x86_acc": "no aes-ni",
        "not-optimized": "not optimized"
    },
)
COLUMN_ALIASES: Dict[str, str] = {
    "iperf-throughput": "Throughput [GiB/s]",
    "disk-throughput": "Throughput [MiB/s]",
    "hdparm-throughput": "Throughput [GiB/s]",
    "SQL statistics read": "Read",
    "SQL statistics write": "Write",
    "Latency (ms) avg": "Latency [ms]",
    "Timing buffer-cache reads": "Cached read [GB/s]",
    "Timing buffered disk reads": "Buffered read [GB/s]",
    "memcopy-size": "Copy size [kB]",
    "memcopy-time": "Latency [ms]",
    "time_per_syscall": "Time [μs]",
    "sqlite-time [s]": "Transactions per second",
    "lat_avg(ms)": "Latency [ms]",
    "req_sec_tot": "Requests/sec",
    "Throughput(ops/sec)": "Throughput [ops/sec]",
    "AverageLatency(us)": "Latency [μs]",
    "sqlite-op-type": "Operation",
    "hdparm_kind": "Read",
    "network-bs-throughput": "Throughput [MiB/s]",
    "batch_size": "Batch size(KiB)",
    "batch-size": "Batch size(KiB)",
    "storage-bs-throughput": "Throughput [MiB/s]",
    "aesnithroughput": "Throughput [MiB/s]",
    "cores": "Jobs",
}


def systems_order(df: pd.DataFrame) -> List[str]:
    priorities = {
        "native": 10,
        "sync": 15,
        "sgx-lkl": 20,
        "async": 20,
        "scone": 30,
        "sgx-io": 40,
        "rkt-io": 40,
        "direct": 40,
    }
    systems = list(df.system.unique())
    return sorted(systems, key=lambda v: priorities.get(v, 100))


def column_alias(name: str) -> str:
    return COLUMN_ALIASES.get(name, name)


def apply_aliases(df: pd.DataFrame) -> pd.DataFrame:
    for column in df.columns:
        aliases = ROW_ALIASES.get(column, None)
        if aliases is not None:
            df[column] = df[column].replace(aliases)
    return df.rename(index=str, columns=COLUMN_ALIASES)


def change_width(ax: Any, new_value: Union[int, float]) -> None:
    for patch in ax.patches:
        current_width = patch.get_width()
        diff = current_width - new_value
        patch.set_width(new_value)

        patch.set_x(patch.get_x() + diff * 0.5)

def apply_to_graphs(ax: Any, legend: bool, legend_cols: int):
    change_width(ax, 0.405)
    # change_width(ax, 0.25)
    ax.set_xlabel("")

    if legend:
        ax.legend(loc="best")

