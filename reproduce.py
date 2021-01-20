#!/usr/bin/env python3

import sys
if sys.version_info < (3, 7, 0):
    print("This script assumes at least python3.7", file=sys.stderr)

import os
import shutil
from typing import IO, Any, Callable, List, Dict, Optional, Text
import subprocess
from pathlib import Path

ROOT = Path(__file__).parent.resolve()
HAS_TTY = sys.stderr.isatty()


def color_text(code: int, file: IO[Any] = sys.stdout) -> Callable[[str], None]:
    def wrapper(text: str) -> None:
        if HAS_TTY:
            print(f"\x1b[{code}m{text}\x1b[0m", file=file)
        else:
            print(text, file=file)

    return wrapper


warn = color_text(31, file=sys.stderr)
info = color_text(32)


def run(
    cmd: List[str],
    extra_env: Dict[str, str] = {},
    stdout: int = subprocess.PIPE,
    input: Optional[str] = None,
    check: bool = True,
    cwd: str = str(ROOT)
) -> "subprocess.CompletedProcess[Text]":
    env = os.environ.copy()
    env.update(extra_env)
    env_string = []
    for k, v in extra_env.items():
        env_string.append(f"{k}={v}")
    info(f"$ {' '.join(env_string)} {' '.join(cmd)}")
    return subprocess.run(
        cmd, cwd=cwd, stdout=stdout, check=check, env=env, text=True, input=input
    )


def build(nix_shell: str, sudo: str) -> None:
    info("Build the project")
    run([nix_shell, "--command", f"make DEBUG=opt SUDO={sudo}"], extra_env=dict(PATH=""))
    info("Done building")


def checkout_submodules(nix_shell: str) -> None:
    info("Checkout git submodules")
    run([nix_shell, "--command", "git submodule update --init"])


def main() -> None:
    nix_shell = shutil.which("nix-shell", mode=os.F_OK | os.X_OK)
    if nix_shell is None:
        warn("For reproducibility this script requires the nix package manager to be installed: https://nixos.org/download.html")
        sys.exit(1)
    sudo = shutil.which("sudo", mode=os.F_OK | os.X_OK)
    if sudo is None:
        warn("During the build phase we need the 'sudo' command set setsuid executables")
        sys.exit(1)
    checkout_submodules(nix_shell)
    build(nix_shell, sudo)


if __name__ == "__main__":
    main()
