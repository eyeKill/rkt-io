{ stdenv, fetchFromGitHub
, enableStatic ? false
, enableGraphene ? false
, openssl
, pkg-config
}:
stdenv.mkDerivation {
  name = "iperf-3.7";
  #src = ../iperf-3.7;
  src = fetchFromGitHub {
    owner = "Mic92";
    repo = "iperf-3.7";
    rev = "3ff810a4ab2939454e5c812b4a7218a1cdda2136";
    sha256 = "081ppw2swjz75zzar3ddn0hsvh6jdig65jhj18jk5xpnvjx58kqj";
  };
  buildInputs = [
    (openssl.override {
      stdenv = stdenv;
      static = enableStatic;
    })
  ];
  nativeBuildInputs = [ pkg-config ];
  iperf3_cv_header_tcp_congestion = "no";
  configureFlags = [ "--disable-profiling" ] ++ stdenv.lib.optionals (enableStatic) [
    "--disable-shared"
    "--enable-static"
  ];
 
  NIX_CFLAGS_COMPILE = stdenv.lib.optionalString stdenv.cc.isGNU "-pthread";
}
