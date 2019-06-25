#!/usr/bin/env python3

import hashlib
import subprocess

from helpers import Settings, create_settings, nix_build, spawn, ROOT


def test_nginx(settings: Settings) -> None:
    nginx = nix_build("nginx")
    remote_curl = settings.remote_command(nix_build("curl-remote"))
    with spawn(nginx.strip()):
        for _ in range(10):
            try:
                curl_args = ["curl", "-s", settings.local_dpdk_ip + "/test/file-3mb"]
                proc = remote_curl.run("bin/curl", curl_args)
                sha256 = hashlib.sha256(proc.stdout).hexdigest()
                expected = (
                    "259da4e49b1d0932c5a16a9809113cf3ea6c7292e827298827e020aa7361f98d"
                )
                assert sha256 == expected, f"{hash} == {expected}"
                break
            except subprocess.CalledProcessError:
                pass


def test_fstest() -> None:
   fstest = ROOT.joinpath("..", "fstest")
   subprocess.run(["make", "-C", fstest, "check"])


def main() -> None:
    settings = create_settings()
    test_nginx(settings)
    test_fstest()


if __name__ == "__main__":
    main()
