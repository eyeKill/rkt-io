#!/usr/bin/env python3
from typing import Dict, List
import pandas as pd

SYSTEM_ALIASES: Dict[str, str] = {
    "sgx-io": "rkt-io"
}
OPERATION_ALIASES: Dict[str, str] = {
    "read-bw": "read",
    "write-bw": "write",
}
ROW_ALIASES = dict(system=SYSTEM_ALIASES, operation=OPERATION_ALIASES)
COLUMN_ALIASES: Dict[str, str] = {
    "throughput": "Throughput [GiB/s]",
    "disk-throughput": "Throughput [MiB/s]",
    "SQL statistics read": "Read",
    "SQL statistics write": "Write",
    "Latency (ms) avg": "Latency [ms]",
    "Timing buffer-cache reads": "Cached read [GB/s]",
    "Timing buffered disk reads": "Buffered read [GB/s]",
    "memcopy-size": "Copy size [kB]",
    "memcopy-time": "Latency [ms]",

    "sqlite-time [s]": "Transactions per second",
    "lat_avg(ms)": "Latency [ms]",
    "req_sec_tot": "Requests/sec",
    "Throughput(ops/sec)": "Throughput [ops/sec]",
    "AverageLatency(us)": "Latency [us]",
    "sqlite-op-type": "Operation",
}


def systems_order(df: pd.DataFrame) -> List[str]:
    priorities = {
        "native": 10,
        "sgx-lkl": 20,
        "scone": 30,
        "sgx-io": 40,
        "rkt-io": 40,
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
