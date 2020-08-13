import json
import os
import subprocess
import time
import signal
import pandas as pd
from io import StringIO
from collections import defaultdict
from typing import Any, DefaultDict, Dict, List, Optional

from helpers import (
    NOW,
    Settings,
    RemoteCommand,
    create_settings,
    flamegraph_env,
    nix_build,
    read_stats,
    write_stats,
    spawn
)
from storage import Storage, StorageKind
from network import Network, NetworkKind

def list_to_csv(val_list: List['str']) -> str:
    return ','.join(val_list)

def process_ycsb_out(
    ycsb_out: str,
    system: str,
    bench_result: List[str]
) -> None:
    csv_file    = StringIO(ycsb_out)
    df          = pd.read_csv(csv_file, header=None)
    csv_headers = list(df.iloc[:, 1].values)
    csv_vals    = list(df.iloc[:, 2].values)
    csv_vals    = [str(i) for i in csv_vals]

    if len(bench_result) == 0:
        csv_headers = list_to_csv(csv_headers)
        csv_headers = f"system, {csv_headers}"
        bench_result.append(csv_headers)

    csv_vals = list_to_csv(csv_vals)
    csv_vals = f"{system},{csv_vals}"
    bench_result.append(csv_vals)

def benchmark_redis(
    settings: Settings,
    system: str,
    redis_server: str,
    remote_ycsb: RemoteCommand,
    stats: Dict[str, List[str]],
    extra_env: Dict[str, str] = {},
) -> None:
    env = extra_env.copy()
    env[
        "SGXLKL_SYSCTL"
    ] = "net.core.rmem_max=56623104;net.core.wmem_max=56623104;net.core.rmem_default=56623104;net.core.wmem_default=56623104;net.core.optmem_max=40960;net.ipv4.tcp_rmem=4096 87380 56623104;net.ipv4.tcp_wmem=4096 65536 56623104;"

    with spawn(redis_server, extra_env=extra_env):
        while True:
            time.sleep(5)
            try:
                print(f"Running wrk benchmark for {system} case")
                load_proc = remote_ycsb.run(
                    "bin/ycsb",
                    [
                        "load",
                        "redis",
                        "-s",
                        "-P",
                        f"{remote_ycsb.nix_path}/share/ycsb/workloads/workloada",
                        "-p",
                        f"redis.host={settings.local_dpdk_ip}",
                        "-p",
                        "redis.port=6379"
                    ]
                )
                break
            except subprocess.CalledProcessError:
                print(".")
                pass

        run_proc = remote_ycsb.run(
                        "bin/ycsb",
                        [
                            "run",
                            "redis",
                            "-s",
                            "-P",
                            f"{remote_ycsb.nix_path}/share/ycsb/workloads/workloada",
                            "-p",
                            f"redis.host={settings.local_dpdk_ip}",
                            "-p",
                            "redis.port=6379"
                        ]
                    )

    if "load_res" not in stats:
        stats["load_res"] = []
    if "run_res" not in stats:
        stats["run_res"] = []
    
    process_ycsb_out(load_proc.stdout, system, stats["load_res"])
    process_ycsb_out(run_proc.stdout, system, stats["run_res"])

def benchmark_redis_native(
    settings: Settings,
    stats: Dict[str, List[str]]
) -> None:
    Network(NetworkKind.NATIVE, settings).setup()
    redis_server = nix_build("redis-native")
    remote_ycsb  = settings.remote_command(nix_build("ycsb-native"))

    benchmark_redis(settings, "native", redis_server, remote_ycsb, stats)

def benchmark_redis_sgx_lkl(
    settings: Settings,
    stats: Dict[str, List[str]]
) -> None:
    Network(NetworkKind.TAP, settings).setup()
    extra_env = dict(
        SGXLKL_IP4=settings.local_dpdk_ip,
        SGXLKL_IP6=settings.local_dpdk_ip6,
        SGXLKL_TAP_OFFLOAD="1",
        SGXLKL_TAP_MTU="1500",
    )

    redis_server = nix_build("redis")
    remote_ycsb  = settings.remote_command(nix_build("ycsb-native"))

    benchmark_redis(settings, "native", redis_server, remote_ycsb, stats, extra_env=extra_env)

def benchmark_redis_sgx_io(
    settings: Settings,
    stats: Dict[str, List[str]]
) -> None:
    Network(NetworkKind.DPDK, settings).setup()
    extra_env = dict(SGXLKL_DPDK_MTU="1500")

    redis_server = nix_build("redis")
    remote_ycsb  = settings.remote_command(nix_build("ycsb-native"))

    benchmark_redis(settings, "native", redis_server, remote_ycsb, stats, extra_env=extra_env)

def main() -> None:
    stats: Dict[str, List[str]] = {}
    settings = create_settings()

    benchmark_redis_native(settings, stats)
    # benchmark_redis_sgx_lkl(settings, stats)
    # benchmark_redis_sgx_io(settings, stats)

    load_df = pd.DataFrame(stats["load_res"])
    run_df  = pd.DataFrame(stats["run_res"])

    load_df.to_csv("redis_load.csv")
    run_df.to_csv("redis_run.csv")

if __name__=="__main__":
    main()