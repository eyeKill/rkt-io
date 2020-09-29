import json
import subprocess
from collections import defaultdict
from typing import Dict, List, DefaultDict, Optional, Any
import pandas as pd

from helpers import (
    NOW,
    nix_build,
    read_stats,
    write_stats,
)

KINDS = {
    "avx": "0",
    "old": "1",
    "libc": "2",
}


def bench_memcpy(kind: str, stats: Dict[str, List]) -> None:
    memcpy = nix_build("memcpy-test-sgx-io")
    stdout: Optional[int] = subprocess.PIPE

    proc = subprocess.Popen([memcpy, "bin/memcpy-test", KINDS[kind]], stdout=stdout, text=True)
    try:
        if proc.stdout is None:
            proc.wait()
        else:
            for line in proc.stdout:
                print(line)
                try:
                    data = json.loads(line)
                    for i in data:
                        stats["memcpy-kind"].append(f"memcpy-test-{kind}")
                        stats["memcpy-size"].append(i)
                        stats["memcpy-time"].append(data[i])
                except Exception as e:
                    continue
    finally:
        pass


def bench_avx_memcpy(stats: Dict[str, List]) -> None:
    bench_memcpy("avx", stats)


def bench_old_memcpy(stats: Dict[str, List]) -> None:
    bench_memcpy("old", stats)


def bench_libc_memcpy(stats: Dict[str, List]) -> None:
    bench_memcpy("libc", stats)


def main() -> None:
    stats: DefaultDict[str, List] = defaultdict(list)

    benchmarks = {
        "avx_memcpy": bench_avx_memcpy,
        "new_memcpy": bench_old_memcpy,
        "libc_memcpy": bench_libc_memcpy
    }

    for name, benchmark in benchmarks.items():
        benchmark(stats)

    csv = f"memcpy-bench-{NOW}.tsv"
    memcpy_df = pd.DataFrame(stats)
    memcpy_df.to_csv(csv, index=False, sep="\t")
    memcpy_df.to_csv("memcpy-bench-latest.tsv", index=False, sep="\t")

if __name__=="__main__":
    main()
