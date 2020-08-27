#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from threading import Condition, Thread
from typing import Any, Dict, List

IPERF3_DEFAULT_PORT = 5201


def run_iperf(
    condition: Condition,
    iperf_cmd: List[str],
    results: List[Dict[str, Any]],
    index: int,
) -> None:
    with condition:
        condition.wait()
        # DEBUG
        # print(f"$ {' '.join(iperf_cmd)}")
        proc = subprocess.run(iperf_cmd, stdout=subprocess.PIPE)
        results[index] = json.loads(proc.stdout.decode("utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Run iperf3 instances in parallel")
    parser.add_argument("instances", type=int, help="number of parallel instances")
    args, base_iperf_cmd = parser.parse_known_args()
    condition = Condition()
    threads = []
    results: List[Dict[str, Any]] = [{} for i in range(args.instances)]
    for i in range(args.instances):
        iperf_cmd = base_iperf_cmd + ["-p", str(IPERF3_DEFAULT_PORT + i)]
        thread_args = (condition, iperf_cmd, results, i)
        thread = Thread(target=run_iperf, args=thread_args)
        thread.start()
        threads.append(thread)

    with condition:
        condition.notify_all()

    for thread in threads:
        thread.join()

    instances = []
    for i, result in enumerate(results):
        instances.append(dict(port=IPERF3_DEFAULT_PORT + 1, result=result))
    json.dump(dict(instances=instances), sys.stdout)


if __name__ == "__main__":
    main()
