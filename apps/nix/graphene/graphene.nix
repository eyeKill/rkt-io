{ stdenv
, fetchurl
, fetchFromGitHub
, python3
, bison
, perl
, makeWrapper
, runCommand
, fetchpatch
, protobuf
, protobufc
, curl
, linux_latest
}:
let
  mbedtlsChecksum = "320e930b7596ade650ae4fc9ba94b510d05e3a7d63520e121d8fdc7a21602db9";
  mbedtlsVersion = "2.21.0";
  mbedtls = fetchurl {
    url = "https://github.com/ARMmbed/mbedtls/archive/mbedtls-${mbedtlsVersion}.tar.gz";
    sha256 = "1f9dc0hpmp4g3l90wlk3glx5xl0hnnabmjagmr8fdbcnfl5r63ij";
  };
  mbedcryptoChecksum = "7e171df03560031bc712489930831e70ae4b70ff521a609c6361f36bd5f8b76b";
  mbedcryptoVersion = "3.1.0";
  mbedcrypto = fetchurl {
    url = "https://github.com/ARMmbed/mbed-crypto/archive/mbedcrypto-${mbedcryptoVersion}.tar.gz";
    sha256 = "0sxpz3anpwv1cff606jjzxq4pbkh3s1k16a82b3in0v06pq1s5vy";
  };

  # see LibOS/glibc-checksums
  glibcChecksum = "cb2d64fb808affff30d8a99a85de9d2aa67dc2cbac4ae99af4500d6cfea2bda7";
  glibcVersion = "2.31";
  glibc = fetchurl {
    url = "https://mirrors.ocf.berkeley.edu/gnu/glibc/glibc-${glibcVersion}.tar.gz";
    sha256 = "19xxlbz6q3ahyjdfjjmcrg17v9iakpg8b6m9v0qgzzwah3xn8bfb";
  };

  cjsonChecksum = "760687665ab41a5cff9c40b1053c19572bcdaadef1194e5cba1b5e6f824686e7";
  cjsonVersion = "1.7.12";
  cjson = fetchurl {
    url = "https://github.com/DaveGamble/cJSON/archive/v${cjsonVersion}.tar.gz";
    sha256 = "1rw68s16yphvp9f4w6givsmcsasp34y0bca0kkzmq6mlb9k8f1kn";
  };

  DL_CACHE = runCommand "dl-cache" {} ''
    mkdir $out
  '';
in stdenv.mkDerivation rec {
  name = "graphene";

  ISGX_DRIVER_PATH = fetchFromGitHub {
    owner = "intel";
    repo = "linux-sgx-driver";
    rev = "sgx_driver_2.6";
    sha256 = "0y5i71p2vzjiq47hy5v3a11iyy5qp8s3v0jgbjjpaqx6hqrpb7bj";
  };

  src = fetchFromGitHub {
    owner = "oscarlab";
    repo = "graphene";
    rev = "9092a649b7e292061b30186338af42f985187839";
    sha256 = "12wk9gqbd92gglwabdf95218gzywn5ms6gyrd01zcp3g75m9lcnm";
    fetchSubmodules = true;
  };

  patches = [
    (fetchpatch {
      url = "https://github.com/oscarlab/graphene/commit/5fe0887e1275ee1cf63816ad67683b397be82172.patch";
      sha256 = "0as1m6c6h7cfwjq05h13829jhxnri0591rakcrnlspkiw5klphg6";
    })
    (fetchpatch {
      url = "https://github.com/oscarlab/graphene/commit/c7704d07b5c9c49031cbbbaf8b4d01782405a67f.patch";
      sha256 = "13dgb2xjc13z3y9aan4hjbywbc8prjsl8nbq070fwdpcm8qg5615";
    })
    (fetchpatch {
      url = "https://github.com/oscarlab/graphene/commit/51b09ef7d18dbd5bc88041438dff37b339d5dc9c.patch";
      sha256 = "1lap130x10xkqycahqy2wkbrnshss69vsbpqjvn38kxra4bg6sgf";
    })
  ];

  DL_OFFLINE = "true";
  DL_CACHE = runCommand "dl-cache" {} ''
    mkdir $out
    ln -s ${mbedtls} $out/${mbedtlsChecksum}
    ln -s ${mbedcrypto} $out/${mbedcryptoChecksum}
    ln -s ${glibc} $out/${glibcChecksum}
    ln -s ${cjson} $out/${cjsonChecksum}
  '';

  postPatch = ''
    patchShebangs ./Scripts Pal/src/host/Linux-SGX/sgx-driver/link-intel-driver.py
  '';

  preBuild = ''
    mkdir -p $out/share/graphene
    # This is insane! Build has references to build directory...
    cp -r . $out/share/graphene
    cd $out/share/graphene
  '';


  KDIR = "${linux_latest.dev}/lib/modules/${linux_latest.modDirVersion}/build";

  # We cannot set RPATH or we break graphen.
  # Therefor we have to manually export our libraries to make ld during the build happy
  LD_LIBRARY_PATH = stdenv.lib.makeLibraryPath buildInputs;
  NIX_DONT_SET_RPATH = "1";
  NIX_NO_SELF_RPATH = "1";

  installPhase = ''
    for lib in Runtime/*.so* Runtime/pal-* Runtime/pal_loader; do
      # copy symlinks and dereferenced lib
      install -D --target $out/Runtime $lib
      install -D --target $out/Runtime $(realpath "$lib")
    done

    buildPythonPath "$out $pythonPath"

    cp -r Pal/src/host/Linux-SGX/{generated_offsets.py,debugger} $out
    cp -r Pal/src/host/Linux-SGX/signer $out/signer
    rm -rf $out/share/graphene
    mkdir -p $out/share/graphene/Pal/src/host/Linux-SGX
    mv $out/{Runtime,signer,generated_offsets.py} $out/share/graphene
    mv $out/debugger $out/share/graphene/Pal/src/host/Linux-SGX

    mkdir -p $out/bin
    for p in $out/share/graphene/signer/{pal-sgx-sign,pal-sgx-get-token}; do
        makeWrapper ${python3.interpreter} "$out/bin/$(basename $p)" \
           --set PYTHONPATH "$program_PYTHONPATH" \
           --add-flags "$p"
    done

    makeWrapper $out/share/graphene/Runtime/pal_loader $out/bin/pal_loader \
      --set LD_LIBRARY_PATH $LD_LIBRARY_PATH \
      --set PAL_HOST Linux-SGX
  '';

  pythonPath = [
    # needed for signing in SGX
    python3.pkgs.protobuf
  ];

  makeFlags = [
    "SGX=1" "DEBUG=1"
  ];

  enableParallelBuilding = true;

  nativeBuildInputs = [
    python3
    python3.pkgs.wrapPython
    bison
    perl
    makeWrapper
    protobuf
    protobufc
  ];

  buildInputs = [
    curl
    protobufc
  ];
}
