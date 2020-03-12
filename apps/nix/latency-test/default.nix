{ stdenv }:

stdenv.mkDerivation {
  name = "latency-test";
  src = ./.;
  installPhase = ''
    mkdir -p $out/bin
    gcc -O2 -o $out/bin/latency-test main.c
  '';
}
