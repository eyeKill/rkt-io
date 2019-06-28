{ stdenv }:

stdenv.mkDerivation {
  name = "dummy-pkg";
  unpackPhase = ":";
  installPhase = "touch $out";
}
