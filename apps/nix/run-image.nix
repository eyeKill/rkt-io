{ runtimeShell, python3, lib, flamegraph, pkgsMusl, writeScript, buildImage }:

{ pkg
, command
, extraFiles ? {}
, extraCommands ? ""
, debugSymbols ? true
, diskSize ? "10G"
, native ? false
}:

let
  image = buildImage {
    inherit pkg extraFiles extraCommands debugSymbols diskSize;
  };
in writeScript "run-lkl" ''
  #!/usr/bin/env bash

  set -xeu -o pipefail

  export TMPDIR=/tmp
  export PATH=${lib.makeBinPath [ flamegraph ]}:$PATH

  if [[ $# -eq 0 ]]; then
    cmd="${toString command}"
  else
    cmd=$1
    shift
  fi
  ${if native then ''
    if [[ -n "''${SGXLKL_CWD:-}" ]]; then
      cd "$SGXLKL_CWD"
    fi
  '' else ""}

  exec ${python3.interpreter} ${./run-image.py} ${if native then "NONE" else image} ${image.pkg}/$cmd "$@"
''
