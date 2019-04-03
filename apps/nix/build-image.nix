{ stdenv, stdenvAdapters, closureInfo
, pkgsMusl, e2fsprogs, lkl, enableDebugging
}:

{ pkg, extraFiles ? {}, debugSymbols ? true, diskSize ? "1G" }:
with stdenv.lib;

let
  piePkg = enableDebugging (pkg.overrideAttrs (old: {
    hardeningEnable = [ "pie" ] ++ (old.hardeningEnable or []);
  }));
  finalPkg = piePkg.override {
    stdenv = if debugSymbols then
      piePkg.stdenv
    else
      piePkg.stdenv;
  };
  closure = closureInfo { rootPaths = [ finalPkg ]; };
  files = {
    "etc/group" = ''
      root:x:0:
      nogroup:x:65534:
    '';
    "etc/passwd" = ''
      root:x:0:0:Root:/:/bin/sh
      nobody:x:65534:65534:Nobody:/:/noshell
    '';
    "etc/hosts" = ''
      127.0.0.1 localhost
      ::1 localhost
    '';
  } // extraFiles;
in stdenv.mkDerivation {
  name = "image";
  buildInputs = [ e2fsprogs lkl ];
  unpackPhase = ":";

  passthru.pkg = finalPkg;

  installPhase = ''
    mkdir -p root/{nix/store,tmp,etc}
    # Filter out musl libc since,
    # sgx-musl will provide its own version.
    # Also filter gcc because it is included in debug symbols.
    grep -v '${pkgsMusl.musl}' ${closure}/store-paths | grep -v '${pkgsMusl.gcc-unwrapped}' \
      | xargs cp -r -t root/nix/store

    ${concatMapStrings (file: ''
      dir="root/$(dirname ${file})"
      if [ ! -d "$dir" ]; then
        mkdir -p "$dir"
      fi
      ${if builtins.isString files.${file} then ''
        cat > root/${file} <<'EOF'
        ${files.${file}}
        EOF
      '' else ''
        install -D ${files.${file}.path} root/${file}
      ''}
    '') (attrNames files)}

    # FIXME calculate storage requirement
    truncate -s ${diskSize}  $out
    mkfs.ext4 $out
    cptofs -t ext4 -i $out root/* /
  '';
}
