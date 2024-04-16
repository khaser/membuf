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
      khaser_kernel = khaser.nixosConfigurations.khaser-nixos.config.boot.kernelPackages.kernel;
      # Module supports following LTS kernel versions:
      supported_kernels = {
        "linux_6.1" = pkgs.linuxKernel.kernels.linux_6_1;
        "linux_6.6" = pkgs.linuxKernel.kernels.linux_6_6;
      };
      configured-vim = khaser.lib.vim.override {
        extraRC = ''
          let &path.="${khaser_kernel.dev}/lib/modules/${khaser_kernel.modDirVersion}/build/source/include"
          set colorcolumn=81
        '';
      };
      pkgForKernel = (kernel: pkgs.callPackage ./default.nix { inherit kernel; });
    in {
      packages = {
        default = (pkgForKernel khaser_kernel);
      } // (builtins.mapAttrs (name: kernel: pkgForKernel kernel) supported_kernels);

      devShell = pkgs.mkShell {
        name = "linux-membuf";

        nativeBuildInputs = with pkgs; [
          gcc # compiler
          configured-vim
        ];

      };
    });
}

