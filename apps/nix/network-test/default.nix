{ stdenv }:

stdenv.mkDerivation {
  name = "network-test";
  src = ./.;
  installPhase = ''
    mkdir -p $out/bin
    gcc -o $out/bin/network-test main.c
  '';
}
