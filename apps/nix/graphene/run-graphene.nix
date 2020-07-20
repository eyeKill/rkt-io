{ stdenv, lib, python, writeScript, graphene, buildPythonApplication, mypy, glibc }:
{ command, pkg, ports ? [] }:

let
  run-graphene = stdenv.mkDerivation rec {
    name = "run-graphene";
    src = ./.;

    buildInputs = [ python ];
    nativeBuildInputs = [ mypy ];
    installPhase = ''
      install -D -m755 run-graphene.py $out/bin/run-graphene
    '';
  };

  pkg' = pkg.overrideAttrs (old: {
    # We cannot set RPATH or we break graphen.
    # Therefor we have to manually export our libraries to make ld during the build happy
    LD_LIBRARY_PATH = stdenv.lib.makeLibraryPath (old.buildInputs or []);
    NIX_DONT_SET_RPATH = "1";
    NIX_NO_SELF_RPATH = "1";
    NIX_DEBUG = "1";
    dontPatchELF = true;
  });
in writeScript "run-graphene" ''
  #!/usr/bin/env bash

  set -xeu -o pipefail

  export TMPDIR=/tmp
  export PATH=${lib.makeBinPath [ graphene ]}:$PATH
  export LD_LIBRARY_PATH=${pkg'.LD_LIBRARY_PATH}:${glibc}/lib:${pkg}/lib

  if [[ $# -eq 0 ]]; then
    cmd="${toString command}"
  else
    cmd=$1
    shift
  fi

  exec ${python.interpreter} ${run-graphene}/bin/run-graphene ${lib.optionalString (ports != []) "--ports"} ${lib.concatStringsSep "," (map toString ports)} ${graphene}/share/graphene/Runtime -- ${pkg'}/$cmd "$@"
''
