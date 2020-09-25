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
    rev = "d99abdfedc4ff70746a81e3eb2e20133a6478f77";
    sha256 = "1cmfcbc87a3g0lafspqsw0sigw05506zqfq0cdhjckwyr1qx51l0";
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
}
