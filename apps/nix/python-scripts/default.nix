{ stdenv, python3Minimal, enableDebugging }:

let
  python = enableDebugging (python3Minimal.overrideAttrs (old: {
    hardeningEnable = [ "pie" ] ++ (old.hardeningEnable or []);
  }));
in stdenv.mkDerivation {
  name = "python-scripts";
  buildInputs = [ python ];
  src = ./.;
  passthru = {
    inherit (python) interpreter;
  };
  installPhase = ''
    install -D --target $out/bin *.py
  '';
}
