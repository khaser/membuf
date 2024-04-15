{
  description = "Environment to develop linux out-of-tree module";

  inputs = {
    nixpkgs.follows = "khaser/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    khaser.url = "git+ssh://git@109.124.253.149:64396/~git/nixos-config?ref=master";
  };

  outputs = { self, nixpkgs, flake-utils, khaser }:
    flake-utils.lib.eachDefaultSystem ( system:
    let
      pkgs = import nixpkgs { inherit system; };
      # kernel = pkgs.linuxKernel.kernels.linux_6_1;
      kernel = khaser.nixosConfigurations.khaser-nixos.config.boot.kernelPackages.kernel;
      configured-vim = khaser.lib.vim.override {
        extraRC = ''
          let &path.="${kernel.dev}/lib/modules/${kernel.modDirVersion}/build/source/include"
          set colorcolumn=81
        '';
      };
    in {
      packages.default = pkgs.callPackage ./default.nix { inherit kernel; };

      devShell = pkgs.mkShell {
        name = "linux-membuf";

        nativeBuildInputs = with pkgs; [
          gcc # compiler
          configured-vim
        ];

      };
    });
}

