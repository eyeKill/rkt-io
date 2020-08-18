import json
import os
import subprocess
import signal
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
    spawn,
)
from storage import Storage, StorageKind
from network import Network, NetworkKind, setup_remote_network
from process_wrk import parse_wrk_output, wrk_data, wrk_csv_cols


def process_wrk_output(wrk_out: str, system: str, bench_result: List[str]) -> None:
    wrk_output_dict = parse_wrk_output(wrk_out)
    # print(wrk_output_dict)
    wrk_output_csv = f"{system}," + wrk_data(wrk_output_dict)
    # print(wrk_output_csv)
    if len(bench_result) == 0:
        col_names = wrk_csv_cols()
        col_names = f"system,{col_names}"
        bench_result.append(col_names)
    bench_result.append(wrk_output_csv)


def benchmark_nginx(
    settings: Settings,
    attr: str,
    system: str,
    nginx_server: str,
    remote_wrk: RemoteCommand,
    wrk_args: List[str],
    bench_result: List[str],
    extra_env: Dict[str, str] = {},
) -> None:
    env = extra_env.copy()
    env[
        "SGXLKL_SYSCTL"
    ] = "net.core.rmem_max=56623104;net.core.wmem_max=56623104;net.core.rmem_default=56623104;net.core.wmem_default=56623104;net.core.optmem_max=40960;net.ipv4.tcp_rmem=4096 87380 56623104;net.ipv4.tcp_wmem=4096 65536 56623104;"

    with spawn(nginx_server, extra_env=env):
        while True:
            try:
                print(f"Running wrk benchmark for {system} case")
                proc = remote_wrk.run("bin/wrk", wrk_args)
                break
            except subprocess.CalledProcessError:
                print(".")
                pass

        process_wrk_output(proc.stdout, system, bench_result)


def benchmark_nginx_native(
    settings: Settings, wrk_args: List[str], bench_result: List[str]
) -> None:
    Network(NetworkKind.NATIVE, settings).setup()

    nginx_server = nix_build("nginx-native")
    remote_wrk = settings.remote_command(nix_build("wrk-bench"))

    wrk_args.append(f"http://{settings.local_dpdk_ip}:9000")

    benchmark_nginx(
        settings,
        "nginx-native",
        "native",
        nginx_server,
        remote_wrk,
        wrk_args,
        bench_result,
    )


def benchmark_nginx_sgx_lkl(
    settings: Settings, wrk_args: List[str], bench_result: List[str]
) -> None:
    Network(NetworkKind.TAP, settings).setup()
    extra_env = dict(
        SGXLKL_IP4=settings.local_dpdk_ip,
        SGXLKL_IP6=settings.local_dpdk_ip6,
        SGXLKL_TAP_OFFLOAD="1",
        SGXLKL_TAP_MTU="1500",
    )

    nginx_server = nix_build("nginx")
    remote_wrk = settings.remote_command(nix_build("wrk-bench"))

    wrk_args.append(f"http://{settings.local_dpdk_ip}:9000")

    benchmark_nginx(
        settings,
        "nginx-sgx-lkl",
        "sgx-lkl",
        nginx_server,
        remote_wrk,
        wrk_args,
        bench_result,
        extra_env=extra_env,
    )


def benchmark_nginx_sgx_io(
    settings: Settings, wrk_args: List[str], bench_result: List[str]
) -> None:
    Network(NetworkKind.DPDK, settings).setup()
    extra_env = dict(SGXLKL_DPDK_MTU="1500")

    nginx_server = nix_build("nginx")
    remote_wrk = settings.remote_command(nix_build("wrk-bench"))

    wrk_args.append(f"http://{settings.local_dpdk_ip}:80")

    benchmark_nginx(
        settings,
        "nginx-sgx-io",
        "sgx-io",
        nginx_server,
        remote_wrk,
        wrk_args,
        bench_result,
        extra_env=extra_env,
    )


def build_mount_iotest() -> None:
    iotest_image = nix_build("iotest-image")

    cmd = ["sudo", "mkdir", "-p", "/tmp/mnt"]
    subprocess.run(cmd)

    cmd = ["sudo", "mount", "iotest-image", "/tmp/mnt"]
    subprocess.run(cmd)


def umount_iotest() -> None:
    cmd = ["sudo", "umount", "/tmp/mnt"]
    subprocess.run(cmd)


def read_wrk_args(file_name: str) -> List[str]:
    f = open(file_name)
    data = json.load(f)

    wrk_args = []
    for j in data:
        if j == "server":
            wrk_args.append(data[j])
        else:
            wrk_args.append(j + str(data[j]))

    return wrk_args


def main() -> None:
    # stats: DefaultDict[str, List] = defaultdict(list)
    bench_result: List[str] = []
    settings = create_settings()
    setup_remote_network(settings)
    wrk_args = read_wrk_args("wrk_args.json")

    build_mount_iotest()

    # benchmark_nginx_native(settings, wrk_args, bench_result)
    # wrk_args = read_wrk_args("wrk_args.json")
    benchmark_nginx_sgx_lkl(settings, wrk_args, bench_result)
    # wrk_args = read_wrk_args("wrk_args.json")
    # benchmark_nginx_sgx_io(settings, wrk_args, bench_result)

    umount_iotest()

    csv = f"nginx-{NOW}.csv"

    with open(csv, "w") as f:
        for b in bench_result:
            f.write(f"{b}\n")


if __name__ == "__main__":
    main()
