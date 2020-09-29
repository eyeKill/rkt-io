import time
import subprocess
from typing import Dict, List
import json
import signal
import os

import pandas as pd

from helpers import (
    NOW,
    Settings,
    create_settings,
    nix_build,
    read_stats,
    write_stats,
    spawn,
    run as helper_run,
)
from storage import Storage, StorageKind
from network import Network, NetworkKind, setup_remote_network, remote_cmd

class Benchmark:
    def __init__(self, settings: Settings) -> None:
        self.settings = create_settings()
        self.storage = Storage(settings)
        self.network = Network(settings)
        self.local_nc = nix_build("netcat-native")

    def run(
        self,
        attr: str,
        system: str,
        stats: Dict[str, List],
        extra_env: Dict[str, str] = {},
    ) -> None:
        env = os.environ.copy()
        env.update(extra_env)

        network_test = nix_build(attr)
        server_ip = self.settings.remote_dpdk_ip
        num_bytes = str(1*1024*1024*1024) # 1 GiB
        batch_size = [4, 8, 16, 32, 64, 128, 256, 512] # in KiB
        #batch_size = [4, 8] # in KiB

        helper_run(["nix", "copy", self.local_nc, "--to", f"ssh://{self.settings.remote_ssh_host}"])

        nc_cmds = [
            ["while", "true"],
            ["do", f"{self.local_nc}/bin/nc", "-l", "8888", ">", "/dev/null", "2>&1"],
            ["done"]
        ]
        nc_command = "; ".join(map(lambda cmd: " ".join(cmd), nc_cmds))

        with spawn("ssh", self.settings.remote_ssh_host, "--", nc_command) as remote_nc_proc:
            for bs in batch_size:
                #while True:
                #    try:
                #        nc_cmd = [
                #            f"{self.local_nc}/bin/nc",
                #            "-z", "-v",
                #            f"{server_ip}",
                #            "8888"
                #        ]
                #        nc_proc = subprocess.run(nc_cmd)
                #        break
                #    except subprocess.CalledProcessError:
                #        #status = remote_nc_proc.poll()
                #        #if status is not None:
                #        #    raise OSError(f"netcat-server exiteded with {status}")
                #        #time.sleep(1)
                #        pass

                local_proc = subprocess.Popen(
                    [
                     network_test,
                     "bin/network-test",
                     "write",
                     f"{server_ip}",
                     num_bytes,
                     str(bs),
                    ],
                    stdout=subprocess.PIPE, text=True, env=env,
                )

                try:
                    local_proc.wait()
                    #breakpoint()
                    assert local_proc.stdout
                    for line in local_proc.stdout:
                        data = json.loads(line)
                        stats["system"].append(system)
                        stats["batch_size"].append(bs)
                        for i in data:
                            stats[i].append(data[i])
                    print(local_proc.stdout.read())
                except Exception as e:
                    print(f"{local_proc.stdout} not in json format")
                print(stats)

def benchmark_nw_test_sgx_lkl(benchmark: Benchmark, stats: Dict[str, List]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.TAP)
    benchmark.run("network-test-sgx-lkl", "sgx-lkl", stats, extra_env=extra_env)

def benchmark_nw_test_sgx_io(benchmark: Benchmark, stats: Dict[str, List]) -> None:
    extra_env = benchmark.network.setup(NetworkKind.DPDK)
    benchmark.run("network-test-sgx-io", "sgx-io", stats, extra_env=extra_env)

def main() -> None:
    stats = read_stats("network-test-bs.json")
    settings = create_settings()
    setup_remote_network(settings)

    benchmark = Benchmark(settings)

    benchmarks = {
        #"sgx-lkl": benchmark_nw_test_sgx_lkl,
        "sgx-io": benchmark_nw_test_sgx_io,
    }

    system = set(stats["system"])
    for name, bench_func in benchmarks.items():
        if name in system:
            print(f"skip {name} benchmark")
            continue
        bench_func(benchmark, stats)
        write_stats("network-test-bs.json", stats)

    csv = f"network-test-bs-{NOW}.tsv"
    throughput_df = pd.DataFrame(stats)
    throughput_df.to_csv(csv, index=False, sep="\t")
    throughput_df.to_csv("network-test-bs-latest.tsv", index=False, sep="\t")

if __name__=="__main__":
    main()
