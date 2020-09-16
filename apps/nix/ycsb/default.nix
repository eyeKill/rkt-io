{ stdenv, fetchurl, python2, jre, makeWrapper }:

stdenv.mkDerivation rec {
  pname = "ycsb";
  version = "0.17.0";

  src = fetchurl {
    url = "https://github.com/harshanavkis/YCSB/releases/download/1/ycsb-0.17.0.tar.gz";
    sha256 = "1q53icy9qi4qwq62a0lzplkp20kbm64bkyhjig9rkgdykg5xknw5";
  };

  buildInputs = [ python2 ];
  nativeBuildInputs = [ makeWrapper ];

  installPhase = ''
    mkdir -p $out/{bin,share/ycsb}
    cp -r * $out/share/ycsb
    makeWrapper $out/share/ycsb/bin/ycsb $out/bin/ycsb \
      --prefix PATH : ${stdenv.lib.makeBinPath [ jre ]}
  '';
}
