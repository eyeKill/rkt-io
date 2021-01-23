#!/usr/bin/env python

import os
import shutil
import signal
import subprocess
import sys
import tempfile
from datetime import datetime
from typing import List, Dict, IO, Iterator, Any
from contextlib import contextmanager

NOW = datetime.now().strftime("%Y%m%d-%H%M%S")


def enable_flamegraph() -> bool:
    return os.environ.get("SGXLKL_ENABLE_FLAMEGRAPH", None) is not None


def enable_traceshark() -> bool:
    return os.environ.get("SGXLKL_ENABLE_TRACESHARK", None) is not None


def enable_tracecmd() -> bool:
    return os.environ.get("SGXLKL_ENABLE_TRACECMD", None) is not None


def enable_perf_hw_counters() -> bool:
    return os.environ.get("SGXLKL_ENABLE_PERF_HW_COUNTER", None) is not None


def traceshark_cmd(perf_data: str) -> List[str]:
    events = [
        "power:cpu_frequency",
        "power:cpu_idle",
        "sched:sched_kthread_stop",
        "sched:sched_kthread_stop_ret",
        "sched:sched_migrate_task",
        "sched:sched_move_numa",
        "sched:sched_pi_setprio",
        "sched:sched_process_exec",
        "sched:sched_process_exit",
        "sched:sched_process_fork",
        "sched:sched_process_free",
        "sched:sched_process_wait",
        "sched:sched_stick_numa",
        "sched:sched_swap_numa",
        "sched:sched_switch",
        "sched:sched_wait_task",
        "sched:sched_wake_idle_without_ipi",
        "sched:sched_wakeup",
        "sched:sched_wakeup_new",
        "sched:sched_waking",
        "cycles",
    ]
    cmd = ["perf", "record", "-o", perf_data]
    for event in events:
        cmd.extend(["-e", event])
    cmd.extend(["-a", "--call-graph=dwarf,20480", "-m", "128M0"])
    return cmd





def get_debugger(perf_data: str) -> List[str]:
    if os.environ.get("SGXLKL_ENABLE_GDB", None) is not None:
        return ["sgx-lkl-gdb", "-ex", "run", "--args"]
    elif os.environ.get("SGXLKL_ENABLE_STRACE", None) is not None:
        return ["strace"]
    elif enable_traceshark():
        return traceshark_cmd(perf_data)
    elif enable_perf_hw_counters():
        events = [
            "cycles",
            "instructions",
            "cache-references",
            "cache-misses",
            "bus-cycles",
            "L1-dcache-loads",
            "L1-dcache-load-misses",
            "L1-dcache-stores",
            "dTLB-loads",
            "dTLB-load-misses",
            "iTLB-load-misses",
            "LLC-loads",
            "LLC-load-misses",
            "LLC-stores"
        ]
        return ["perf", "record", "-e", ",".join(events), "-o", perf_data, "-a", "-g", "-F99", "--"]
    elif enable_flamegraph():
        return ["perf", "record", "-o", perf_data, "-a", "-g", "-F999", "--"]
    elif enable_tracecmd():
        # to be used as user it requires:
        # $ sudo chmod -R o+rw /sys/kernel/debug
        return [
            "trace-cmd", "record", "-d",
            "--func-stack",
            "-e", "sched_wakeup*",
            "-e", "sched_switch",
            "-e", "sched_migrate*",
            "--"
        ]
    return []


def post_process_perf(perf_data: str, flamegraph: str) -> None:
    perf = subprocess.Popen(["perf", "script", "-i", perf_data],
                            stdout=subprocess.PIPE)
    assert perf.stdout is not None
    perf_script = os.environ.get("PERF_SCRIPT_FILENAME", None)
    perf_stdout = perf.stdout
    if perf_script is not None:
        with open(perf_script, "wb") as perf_script_fd:
            shutil.copyfileobj(perf_stdout, perf_script_fd) # type: ignore
        perf_stdout = open(perf_script, "rb")

    if not enable_flamegraph():
        return

    stackcollapse = subprocess.Popen(
        ["stackcollapse-perf.pl"], stdin=perf_stdout, stdout=subprocess.PIPE
    )
    with open(flamegraph, "w") as f:
        flamegraph_proc = subprocess.Popen(
            ["flamegraph.pl"], stdin=stackcollapse.stdout, stdout=f
        )
    flamegraph_proc.communicate()
    if perf_script is not None:
        perf_stdout.close()


@contextmanager
def debug_mount_env(image: str) -> Iterator[Dict[str, str]]:
    env = os.environ.copy()
    if image == "NONE" or not (enable_flamegraph() or enable_traceshark() or enable_perf_hw_counters()):
        yield env
        return

    with tempfile.TemporaryDirectory(prefix="dbgmount-") as tmpdirname:
        subprocess.run(["sudo", "mount", image, tmpdirname])
        env["SGXLKL_DEBUGMOUNT"] = tmpdirname

        try:
            yield env
        finally:
            subprocess.run(["sudo", "umount", tmpdirname])


def run(
    sgx_lkl_run: str,
    image: str,
    debugger: List[str],
    cmd: List[str],
    env: Dict[str, str],
    tmpdirname: str,
) -> None:
    native_mode = image == "NONE"

    if native_mode:
        complete_cmd = debugger + cmd
    else:
        tmp_fsimage = os.path.join(tmpdirname, "fs.img")
        shutil.copyfile(image, tmp_fsimage)
        complete_cmd = debugger + [sgx_lkl_run, tmp_fsimage] + cmd

    print(" ".join(complete_cmd), file=sys.stderr)
    proc = subprocess.Popen(complete_cmd, env=env)

    def stop_proc(signum: Any, frame: Any) -> None:
        proc.terminate()
        try:
            proc.wait(timeout=1)
        except subprocess.TimeoutExpired:
            proc.send_signal(signal.SIGKILL)

    signal.signal(signal.SIGINT, stop_proc)
    signal.signal(signal.SIGTERM, stop_proc)
    proc.wait()


def main(args: List[str]) -> None:
    if len(args) < 3:
        print(f"USAGE: {sys.argv[0]} sgx-lkl-run image cm", file=sys.stderr)
        sys.exit(1)

    sgx_lkl_run = args[1]
    image = args[2]
    cmd = args[3:]

    with tempfile.TemporaryDirectory(prefix="run-image-") as tmpdirname:
        perf_data = os.path.join(tmpdirname, "perf.data")
        debugger = get_debugger(perf_data)
        with debug_mount_env(image) as env:
            try:
                run(sgx_lkl_run, image, debugger, cmd, env, tmpdirname)
            finally:
                if not enable_flamegraph() and not enable_traceshark() and not enable_perf_hw_counters():
                    return

                flamegraph = os.environ.get("FLAMEGRAPH_FILENAME", None)
                if flamegraph is None:
                    flamegraph = os.path.join(tmpdirname,
                                              f"flamegraph-{NOW}.svg")
                post_process_perf(perf_data, flamegraph)
                perf = os.environ.get("PERF_FILENAME", None)
                if perf is not None:
                    shutil.move(perf_data, perf)


if __name__ == "__main__":
    main(sys.argv)
