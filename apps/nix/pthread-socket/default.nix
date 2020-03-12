{ stdenv }:

stdenv.mkDerivation {
  name = "pthread-socket";
  src = ./.;
  installPhase = ''
    mkdir -p $out/bin
    gcc -o $out/bin/pthread-socket -pthread main.c
  '';
}
