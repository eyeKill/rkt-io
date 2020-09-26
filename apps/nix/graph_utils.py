#!/usr/bin/env python3
from typing import Dict
import pandas as pd

SYSTEM_ALIASES: Dict[str, str] = {
    "sgx-io": "rkt-io"
}
ROW_ALIASES = dict(system=SYSTEM_ALIASES)
COLUMN_ALIASES: Dict[str, str] = {
    "throughput": "Throughput [GiB/s]",
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


def sort_systems(df: pd.DataFrame) -> pd.DataFrame:
    priorities = {
        "native": 10,
        "sgx-lkl": 20,
        "scone": 30,
        "sgx-io": 40,
    }

    def apply_priority(col: pd.Series) -> pd.Series:
        return col.map(lambda v: priorities.get(v, 100))

    return df.sort_values(by='system', key=apply_priority)


def column_alias(name: str) -> str:
    return COLUMN_ALIASES.get(name, name)


def apply_aliases(df: pd.DataFrame) -> pd.DataFrame:
    for column in df.columns:
        aliases = ROW_ALIASES.get(column, None)
        if aliases is not None:
            df[column] = df[column].replace(aliases)
    return df.rename(index=str, columns=COLUMN_ALIASES)
