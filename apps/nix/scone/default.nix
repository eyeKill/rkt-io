{ buildPackages
, runCommand
, stdenv
, skopeo
, oci-image-tool
, cacert
, lib
, autoPatchelfHook
, buildFHSUserEnv
, gcc7
, libredirect
, makeWrapper
, zlib
, gcc
, glibc
, utillinux
, wrapCCWith
, wrapBintoolsWith
, binutils-unwrapped
, overrideCC
, fetchurl
}:
let
  ociImage = fetchurl {
    #sudo skopeo copy --insecure-policy docker-daemon:sconecuratedimages/crosscompilers:ubuntu oci:image:1.0.0
    name = "scone.tar";
    url = "https://cloudflare-ipfs.com/ipfs/QmYbr1iESCCKHCS7iJjZw81jig73yHfBc5ruAhPx9YodV8";
    sha256 = "1jgi3qlwq9bfjqqmcd5rrg68q0a3jbirziff5gzsnlwq12lkpqhk";
  };
  scone-image = runCommand "scone-image" {
    nativeBuildInputs = [ oci-image-tool ];
  } ''
      tar -xf ${ociImage}
      oci-image-tool unpack -ref name=scone image $out
  '';
  gcc-nolibc = wrapCCWith {
    inherit (gcc) cc;
    bintools = wrapBintoolsWith {
      bintools = binutils-unwrapped;
      libc = null;
    };
    extraBuildCommands = ''
      sed -i '2i if ! [[ $@ == *'musl-gcc.specs'* ]]; then exec ${gcc}/bin/gcc -L${glibc}/lib -L${glibc.static}/lib "$@"; fi' \
        $out/bin/gcc

      sed -i '2i if ! [[ $@ == *'musl-gcc.specs'* ]]; then exec ${gcc}/bin/g++ -L${glibc}/lib -L${glibc.static}/lib "$@"; fi' \
        $out/bin/g++

      sed -i '2i if ! [[ $@ == *'musl-gcc.spec'* ]]; then exec ${gcc}/bin/cpp "$@"; fi' \
        $out/bin/cpp
    '';
  };
in rec {
  scone-unwrapped = stdenv.mkDerivation {
    name = "scone-unwrapped";
    dontUnpack = true;

    passthru = {
      isGNU = true;
      hardeningUnsupportedFlags = [ "pie" ];
    };

    installPhase = ''
      mkdir -p $out/{opt,usr/lib/,bin}
      cp -r ${scone-image}/opt/scone $out/opt/scone
      chmod -R +w $out/opt/scone

      for path in $(grep -I -l -R /opt/scone "$out/opt/scone" | xargs readlink -f | sort -u); do
        substituteInPlace "$path" \
          --replace "/opt/scone" "$out/opt/scone"
      done

      for i in gcc cc cpp; do
        makeWrapper $out/opt/scone/bin/scone-gcc $out/bin/$i \
          --set REALGCC ${gcc-nolibc}/bin/gcc \
          --prefix PATH : ${utillinux}/bin
      done
      for i in g++ c++; do
        makeWrapper $out/opt/scone/bin/scone-g++ $out/bin/$i \
          --set REALGCC ${gcc-nolibc}/bin/gcc \
          --prefix PATH : ${utillinux}/bin
      done

      ln -s $out/opt/scone/cross-compiler/x86_64-linux-musl/lib $out/lib
      ln -s $out/opt/scone/bin/scone $out/bin/scone
    '';
    nativeBuildInputs = [
      autoPatchelfHook makeWrapper
    ];
  };
  scone = wrapCCWith {
    cc = scone-unwrapped;
    bintools = wrapBintoolsWith {
      bintools = binutils-unwrapped;
      libc = scone-unwrapped;
    };
  };
  sconeStdenv = overrideCC stdenv scone;

  # for nix-shell
  sconeEnv = sconeStdenv.mkDerivation {
    name = "scone-env";
    hardeningDisable = [ "all" ];
  };
}
