{ runtimeShell, lib, flamegraph, pkgsMusl, writeScript, buildImage }:

{ pkg, command, extraFiles ? {}, extraCommands ? "", debugSymbols ? true, diskSize ? "10G" }:

let
  image = buildImage { inherit pkg extraFiles extraCommands debugSymbols diskSize; };
in writeScript "run-lkl" ''
  #!${runtimeShell}
  set -eu -o pipefail -x

  export PATH=${lib.makeBinPath [ flamegraph ]}:$PATH

  if [[ $# -eq 0 ]]; then
    cmd="${toString command}"
  else
    cmd=$1
    shift
  fi

  tmppath=$(mktemp -d)
  cleanup() {
    if [[ -n "''${pid:-}" ]]; then
      kill $pid
    fi

    if [[ ''${SGXLKL_ENABLE_FLAMEGRAPH:-} ]]; then
      flamegraph="''${FLAMEGRAPH_FILENAME:-${image.pkg.name}-$(date +%s).svg}"

      perf script -i $tmppath/perf.data \
        | stackcollapse-perf.pl \
        | flamegraph.pl > $flamegraph
      echo $flamegraph
    fi

    rm -rf "$tmppath";
  }
  trap cleanup EXIT

  DEBUGGER=
  if [[ -n ''${SGXLKL_ENABLE_GDB:-} ]]; then
    DEBUGGER="sgx-lkl-gdb"
    DEBUGGER="$DEBUGGER --args"
  elif [[ -n ''${SGXLKL_ENABLE_STRACE:-} ]]; then
    DEBUGGER="strace"
  elif [[ -n ''${SGXLKL_ENABLE_FLAMEGRAPH:-} ]]; then
    DEBUGGER="perf record -o $tmppath/perf.data -g -F99 --"
  fi

  install -m660 ${image} $tmppath/fs.img

  if [[ -n "$DEBUGGER" ]]; then
    TMPDIR=/tmp $DEBUGGER sgx-lkl-run $tmppath/fs.img ${image.pkg}/$cmd "$@"
  else
    TMPDIR=/tmp sgx-lkl-run $tmppath/fs.img ${image.pkg}/$cmd "$@" &
    pid=$!
    wait $pid
  fi
''
