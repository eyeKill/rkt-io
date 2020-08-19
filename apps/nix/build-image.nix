{ stdenv, stdenvAdapters, closureInfo
, pkgsMusl, e2fsprogs, lkl, enableDebugging
, lib, scone ? null
}:

{ pkg
, extraFiles ? {}
, extraCommands ? ""
, debugSymbols ? true
, diskSize ? "1G"
, sconeEncryptedDir ? null
}:
let
  piePkg = enableDebugging (pkg.overrideAttrs (old: {
    hardeningEnable = [ "pie" ] ++ (old.hardeningEnable or []);
  }));

  finalPkg = piePkg.override {
    stdenv = if debugSymbols then
      stdenvAdapters.keepDebugInfo piePkg.stdenv
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
  nativeBuildInputs = [ e2fsprogs lkl ] ++ lib.optional (sconeEncryptedDir != null) scone;
  dontUnpack = true;

  passthru.pkg = finalPkg;

  installPhase = ''
    mkdir -p root/{nix/store,tmp,etc}
    # Filter out musl libc since,
    # sgx-musl will provide its own version.
    # Also filter gcc because it is included in debug symbols.
    root=$(readlink -f root)
    grep -v '${pkgsMusl.musl}' ${closure}/store-paths | grep -v '${pkgsMusl.gcc-unwrapped}' \
      | xargs cp -r -t "$root/nix/store"

    ${lib.concatMapStrings (file: ''

      dir="$root/$(dirname ${file})"
      if [ ! -d "$dir" ]; then
        mkdir -p "$dir"
      fi
      ${if builtins.isString files.${file} then ''
        cat > "$root/${file}" <<'EOF'
        ${files.${file}}
        EOF
      '' else ''
        install -D ${files.${file}.path} "$root/${file}"
      ''}
    '') (lib.attrNames files)}

    ${extraCommands}

    ${lib.optionalString (sconeEncryptedDir != null) ''
      mkdir cryptroot
      # creates empty .scone/state.env
      export HOME=$TMPDIR/scone

      pushd cryptroot
      scone fspf create fspf.pb

      scone fspf addr fspf.pb / --kernel / --not-protected
      scone fspf addr fspf.pb ${sconeEncryptedDir} --encrypted --kernel ${sconeEncryptedDir}
      scone fspf addf fspf.pb ${sconeEncryptedDir} "$root" .
      scone fspf encrypt fspf.pb > .scone-keytag
      root=$(readlink -f .)
      popd
    ''}

    # FIXME calculate storage requirement
    truncate -s ${diskSize}  $out
    mkfs.ext4 $out
    cptofs -t ext4 -i $out $root/* $root/.* /
  '';
}
