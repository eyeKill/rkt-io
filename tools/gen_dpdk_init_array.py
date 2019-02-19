#!/usr/bin/env python3

import os
import struct
import subprocess
import sys
import tempfile
from glob import glob
from pathlib import Path
from typing import List


def get_init_functions(lib: str) -> List[str]:
    with tempfile.TemporaryDirectory() as tempdir:
        subprocess.run(["ar", "x", lib], cwd=tempdir, check=True)
        proc = subprocess.run(
            ["readelf", "-r", "--wide"] + glob(os.path.join(tempdir, "*.o")),
            stdout=subprocess.PIPE,
        )
        init_array = False
        symbols = []
        for line in proc.stdout.decode("utf-8").split("\n"):
            if line.startswith("Relocation section '.rela.init_array"):
                init_array = True
                continue
            elif init_array and line.startswith("Relocation section"):
                init_array = False
            elif init_array and "Offset" not in line and line != "":
                # example
                # 0             1            2                 3                4                      5 6
                # 000000000000  017c00000001 R_X86_64_64       000000000000179e mp_hdlr_init_ops_mp_mc + 0
                fields = line.split()
                symbol = fields[4]
                sym_offset = fields[6]
                if symbol == ".text" or sym_offset != "0":
                    raise Exception(
                        f"initializer in {lib} found that has no symbol. Function needs to be non-static!:\n{line}"
                    )
                symbols.append(symbol)
    return symbols


def get_link_order(linker_script: str, linker_flags: List[str]):
    with tempfile.TemporaryDirectory() as tempdir:
        cmd = [
            "gcc",
            "-g",
            "-Wl,-T",
            linker_script,
            "-fPIC",
            "-ldl",
            "-pthread",
            "-o",
            "main",
            "main.c",
        ] + linker_flags
        with open(os.path.join(tempdir, "main.c"), "w+") as f:
            f.write(
                """
void main() {}
"""
            )
        subprocess.run(cmd, cwd=tempdir, check=True)
        main = os.path.join(tempdir, "main")
        objdump_proc = subprocess.run(
            [
                "objcopy",
                "main",
                "/dev/null",
                "--dump-section",
                ".init_array=init_array",
            ],
            check=True,
            cwd=tempdir,
        )
        addrs = []
        with open(os.path.join(tempdir, "init_array"), "rb") as f2:
            section = f2.read()
            for i in range(len(section) // 8):
                addrs.append(hex(struct.unpack("P", section[i * 8:(i + 1) * 8])[0]))

        addr_str = "\n".join(addrs).encode("utf-8")
        addr2line_proc = subprocess.run(
            ["addr2line", "-f", "-e", main], input=addr_str, stdout=subprocess.PIPE
        )
        lines = addr2line_proc.stdout.split(b"\n")
        symbols = []
        for i, line in enumerate(lines):
            if i % 2 == 0 and len(line) > 0:
                symbols.append(line.decode("utf-8"))
        return symbols


# This is literally a linker script. DPDK generates tons of initializers that
# are not called, since sgx-lkl is compiled with -nostdlib. This script scans
# all libraries and puts all initialisers in one function.
def main() -> None:
    if len(sys.argv) < 3:
        print("USAGE: %s OUTPUT LINKERFLAGS", file=sys.stderr)
        sys.exit(1)

    output = sys.argv[1]
    linker_script = os.path.realpath(sys.argv[2])
    linker_flags = sys.argv[3].split()
    symbols: List[str] = []
    lib_path = []

    for flag in linker_flags:
        if flag.startswith("-L"):
            lib_path.append(flag[2:])

    for flag in linker_flags:
        if flag.startswith("-l"):
            libname = flag[2:]
            for p in lib_path:
                libpath = Path(p).joinpath(f"lib{libname}.a")
                if libpath.exists():
                    symbols += get_init_functions(str(libpath))
                    break
            else:
                raise Exception(f"{libname} not found in libpath {lib_path}")

    symbols = get_link_order(linker_script, linker_flags)

    with open(output, "w") as f:
        f.write("void dpdk_init_array() {\n")
        for symbol in symbols:
            if symbol in ["frame_dummy"]:
                continue
            f.write(f"\tvoid {symbol}();\n")
            f.write(f"\t{symbol}();\n")
        f.write("}\n")


if __name__ == "__main__":
    main()
