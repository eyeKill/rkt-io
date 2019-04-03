{ runtimeShell, pkgsMusl, pwndbg, writeScript, buildImage }:

{ pkg, command, extraFiles ? {}, debugSymbols ? true, diskSize ? "10G" }:

let
  image = buildImage { inherit pkg extraFiles debugSymbols diskSize; };
in writeScript "run-lkl" ''
  #!${runtimeShell}
  set -eu -o pipefail -x

  if [[ $# -eq 0 ]]; then
    cmd="${toString command}"
  else
    cmd=$1
    shift
  fi

  tmppath=$(mktemp -d)
  cleanup() {
    if [[ -n "$pid" ]]; then
      kill $pid
    fi
    rm -rf "$tmppath";
  }
  trap cleanup EXIT SIGINT SIGQUIT ERR

  DEBUGGER=
  if [[ -n ''${SGXLKL_ENABLE_GDB:-} ]]; then
    DEBUGGER="sgx-lkl-gdb"
    if [[ -n ''${SGXLKL_ENABLE_PWNDBG:-} ]]; then
      DEBUGGER="$DEBUGGER -x ${pwndbg}/share/pwndbg/gdbinit.py"
    fi
    DEBUGGER="$DEBUGGER --args"
  elif [[ -n ''${SGXLKL_ENABLE_STRACE:-} ]]; then
    DEBUGGER="strace"
  fi

  install -m660 ${image} $tmppath/fs.img
  TMPDIR=/tmp $DEBUGGER sgx-lkl-run $tmppath/fs.img ${image.pkg}/$cmd "$@" &
  pid=$!
  wait $pid
''
