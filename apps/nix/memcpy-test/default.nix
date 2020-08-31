{ stdenv }:

stdenv.mkDerivation {
  name = "memcpy-test";
  src = ./.;
  installPhase = ''
    mkdir -p $out/bin
    gcc -o $out/bin/memcpy-test main.c memcpy_org.s
  '';
}
