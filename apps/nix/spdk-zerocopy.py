import os
import json
import signal
from typing import Dict, List, Optional
import subprocess
import pandas as pd

from helpers import (
    NOW,
    create_settings,
    nix_build,
    read_stats,
    write_stats,
)
from storage import Storage, StorageKind


def benchmark_simpleio(
    storage: Storage,
    type: str,
    attr: str,
    directory: str,
    stats: Dict[str, List],
    extra_env: Dict[str, str] = {},
) -> None:
    env = dict(SGXLKL_CWD=directory)
    env.update(extra_env)
    env.update(SGXLKL_ENABLE_SGXIO="1")
    env.update(SGXLKL_ETHREADS="1")
    env.update(extra_env)
    simpleio = nix_build(attr)
    stdout: Optional[int] = subprocess.PIPE
    if os.environ.get("SGXLKL_ENABLE_GDB", "0") == "1":
        stdout = None
    size = str(10 * 1024 * 1024 * 1024)  # 2G

    env_string = []
    for k, v in env.items():
        env_string.append(f"{k}={v}")
    report = ""
    in_results = False
    env = os.environ.copy()
    env.update(extra_env)

    cmd = [
        simpleio,
        "bin/simpleio",
        "/dev/mapper/spdk0",
        size,
        "0",
        "1",
        str(128 * 4096),
    ]
    print(f"$ {' '.join(env_string)} {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, stdout=stdout, text=True, env=env)
    try:
        assert proc.stdout is not None
        for line in proc.stdout:
            print(f"stdout: {line}", end="")
            if line == "<result>\n":
                in_results = True
            elif in_results and line == "</result>\n":
                break
            elif in_results:
                report = line
    finally:
        proc.send_signal(signal.SIGINT)
    jsondata = json.loads(report)
    stats["type"].append(type)
    stats["bytes"].append(jsondata["bytes"])
    stats["time"].append(jsondata["time"])


def main() -> None:
    stats = read_stats("spdk-zerocopy.json")
    storage = Storage(create_settings())

    mount = storage.setup(StorageKind.SPDK)
    with mount as mnt:
        extra_env1 = mount.extra_env()
        extra_env1["SGXLKL_SPDK_ZEROCOPY"] = "1"
        benchmark_simpleio(
            storage, "optimized", "simpleio-sgx-io", mnt, stats, extra_env=extra_env1
        )
        import time
        time.sleep(5)
        extra_env2 = mount.extra_env()
        extra_env2["SGXLKL_SPDK_ZEROCOPY"] = "0"
        benchmark_simpleio(
            storage, "not-optimized", "simpleio-sgx-io", mnt, stats, extra_env=extra_env2
        )
    write_stats("spdk-zerocopy.json", stats)

    csv = f"spdk-zerocopy-{NOW}.tsv"
    print(csv)
    df = pd.DataFrame(stats)
    df.to_csv(csv, index=False, sep="\t")
    df.to_csv("spdk-zerocopy-latest.tsv", index=False, sep="\t")


if __name__ == "__main__":
    main()
