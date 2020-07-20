#!/usr/bin/env python

import re
import subprocess
import sys
import argparse
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import List
from distutils.spawn import find_executable


def get_libs(exe: str) -> List[Path]:
    libs = []
    prog = subprocess.run(["ldd", exe], capture_output=True, check=True, text=True)
    lines = prog.stdout.split("\n")
    for line in lines:
        match = re.match(r"\t\S+ => (\S+).*", line)
        if not match:
            continue
        libs.append(Path(match.group(1)))
    return libs


def generate_manifest(graphene: Path, prog: Path, ports: List[str]) -> str:
    original_libs = get_libs(str(prog))
    library_path = set()

    libs = []
    for lib in original_libs:
        substitute = graphene.joinpath(lib.name)
        if substitute.exists():
            print(f"remove: {lib}")
            libs.append(substitute)
        else:
            print(f"add: {lib}")
            library_path.add(lib.parent)
            libs.append(lib)

    ld_library_path = ":".join(map(str, [Path("/lib")] + list(library_path)))
    manifest = f"""
loader.exec = file:{prog}
loader.argv0_override = {prog.name}
loader.insecure__use_cmdline_argv = 1
loader.debug_type = none
loader.env.LD_LIBRARY_PATH = {ld_library_path}
loader.preload = file:{graphene}/libsysdb.so

fs.mount.tmp.type = chroot
fs.mount.tmp.path = /tmp
fs.mount.tmp.uri = file:/tmp
sgx.thread_num=8
sgx.allowed_files.tmp = file:/tmp
sgx.allowed_files.profile = file:/tmp/fio-rand-RW.fio
sgx.allowed_files.benchmarkfile = file:/tmp/fio-rand-RW

fs.mount.nvme.type = chroot
fs.mount.nvme.path = /mnt/nvme
fs.mount.nvme.uri = file:/mnt/nvme
sgx.allowed_files.jobfile = file:/mnt/nvme/fio-rand-RW.job
sgx.allowed_files.fiofilename = file:/mnt/nvme/fio-rand-RW

# libc
fs.mount.libc.type = chroot
fs.mount.libc.path = /lib
fs.mount.libc.uri = file:{graphene}
    """

    for i, path in enumerate(library_path):
        manifest += f"""
fs.mount.lib{i}.type = chroot
fs.mount.lib{i}.path = {path}
fs.mount.lib{i}.uri = file:{path}
        """

    for lib in libs:
        match = re.match(r".+/([^/]+)\.so(\.\d+)?$", str(lib))
        assert match is not None
        normalized_name = match.group(1).replace("-", "")
        manifest += f"""
sgx.trusted_files.{normalized_name} = file:{lib}
        """

    for i, port in enumerate(ports):
        manifest += f"""
net.allow_bind.port{i} = :{port}
        """

    return manifest


def run(cmd: List[str], cwd: Path) -> None:
    print("$ " + " ".join(cmd))
    subprocess.run(cmd, check=True, cwd=cwd)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description='Run app in graphene-sgx')
    parser.add_argument('--ports', help='Ports to whitelist')
    parser.add_argument('graphene_runtime', help='Path to graphene runtime', nargs=1)
    parser.add_argument('executable', help='Executable to run', nargs=1)
    parser.add_argument('args', help='Arguments to pass to executable', nargs="*")

    return parser.parse_args()


def main() -> None:
    args = parse_args()
    graphene_runtime = Path(args.graphene_runtime[0]).resolve()
    prog = Path(args.executable[0]).resolve()
    prog_name = prog.name
    prog_args = args.args
    ports = args.ports or ""
    with TemporaryDirectory() as tempdir:
        path = Path(tempdir)
        manifest = path.joinpath(f"{prog_name}.manifest")
        with open(manifest, "w+") as f:
            f.write(generate_manifest(graphene_runtime, prog, ports=ports.split(",")))

        key = path.joinpath("enclave-key.pem")
        run(["openssl", "genrsa", "-out", str(key), "-3", "3072"], cwd=path)

        sgx_manifest = str(manifest) + ".sgx"
        run(
            [
                "pal-sgx-sign",
                "-libpal",
                str(graphene_runtime.joinpath("libpal-Linux-SGX.so")),
                "-key",
                str(key),
                "-manifest",
                str(manifest),
                "-output",
                str(sgx_manifest),
            ], cwd=path
        )

        run(
            [
                "pal-sgx-get-token",
                "-output",
                str(path.joinpath(f"{prog_name}.token")),
                "-sig",
                str(path.joinpath(f"{prog_name}.sig")),
            ], cwd=path
        )

        pal_loader = find_executable("pal_loader")
        if pal_loader is None:
            print("Cannot find command pal_loader")
            sys.exit(1)
        print(f"cd {tempdir} && " + " ".join([pal_loader, str(sgx_manifest)] + prog_args))
        run([pal_loader, str(sgx_manifest)] + prog_args, cwd=path)


if __name__ == "__main__":
    main()
