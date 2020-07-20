{ stdenv }:

stdenv.mkDerivation {
  name = "simpleio";
  src = ./.;
  installFlags = [ "PREFIX=$(out)" ];
}
