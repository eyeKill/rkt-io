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

def bench_memcpy(kind: str, stats: Dict[str, List]) -> None:
    memcpy = nix_build(f"memcpy-test-{kind}")
    stdout: Optional[int] = subprocess.PIPE

    proc = subprocess.Popen([memcpy], stdout=stdout, text=True)
    try:
        if proc.stdout is None:
            proc.wait
        else:
            for line in proc.stdout:
                try:
                    data = json.loads(line)
                    stats["kind"].append(kind)
                    for i in data:
                        stats[i].append(data[i])
                except Exception as e:
                    continue
    finally:
        pass

def bench_old_memcpy(stats: Dict[str, List]) -> None:
    bench_memcpy("old", stats)

def bench_new_memcpy(stats: Dict[str, List]) -> None:
    bench_memcpy("new", stats)

def main() -> None:
    stats: DefaultDict[str, List] = defaultdict(list)

    benchmarks = {
        "old_memcpy": bench_old_memcpy,
        "new_memcpy": bench_new_memcpy
    }

    for name, benchmark in benchmarks.items():
        benchmark(stats)

    csv = f"memcpy-bench-{NOW}.tsv"
    memcpy_df = pd.DataFrame(stats)
    memcpy_df.to_csv(csv, index=False, sep="\t")
    memcpy_df.to_csv("memcpy-bench-latest.tsv", index=False, sep="\t")

if __name__=="__main__":
    main()
