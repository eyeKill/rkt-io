{ python3, callPackage }:

rec {
  graphene = callPackage ./graphene.nix {};
  runGraphene = python3.pkgs.callPackage ./run-graphene.nix {
    graphene = callPackage ./graphene.nix {};
  };
}
