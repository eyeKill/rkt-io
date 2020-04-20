{ stdenv }:

stdenv.mkDerivation {
  name = "write-test";
  src = ./.;
  installFlags = [ "PREFIX=$(out)" ];
}
