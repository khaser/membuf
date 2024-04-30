{ stdenv, kernel }:
let
  version = "0.2.0";
in
stdenv.mkDerivation {
  inherit version;
  name = "membuf-${version}-${kernel.version}";
  src = ./src;
  installPhase = ''
    mkdir $out
    cp -r * $out/
  '';
  # installTargets = [ "install" ];

  hardeningDisable = [ "pic" "format" ];
  nativeBuildInputs = kernel.moduleBuildDependencies;

  KERNELRELEASE = "${kernel.modDirVersion}";
  KERNEL_DIR = "${kernel.dev}/lib/modules/${kernel.modDirVersion}/build";

  makeFlags = [
    "INSTALL_MOD_PATH=$(out)"
  ];
}
