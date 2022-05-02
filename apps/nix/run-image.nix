{ runtimeShell, python3, lib, flamegraph, pkgsMusl, writeScript, buildImage }:

{ pkg
, command
, extraFiles ? {}
, interpreter ? null
, sgx-lkl-run ? "/root/rkt-io/build/sgx-lkl-run"
, extraCommands ? ""
, debugSymbols ? true
, diskSize ? "1G"
, native ? false
}:

let
  image = buildImage {
    inherit pkg extraFiles extraCommands debugSymbols diskSize;
  };
in (writeScript "run-lkl" ''
  #!/usr/bin/env bash

  set -eu -o pipefail

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

  set -x
  exec ${python3.interpreter} ${./run-image.py} ${sgx-lkl-run} ${if native then "NONE" else image} ${toString interpreter} ${image.pkg}/$cmd "$@"
'').overrideAttrs (old: {
  passthru.pkg = pkg;
})
