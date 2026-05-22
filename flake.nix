# SPDX-FileCopyrightText: 2026 Hadi Chokr <hadichokr@icloud.com>
# SPDX-License-Identifier: BSD-3-Clause
{
  description = "xwaylandvideobridge";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in {
      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.kdePackages.callPackage (
            {
              stdenv,
              cmake,
              extra-cmake-modules,
              pkg-config,
              wrapQtAppsHook,
              qtbase,
              qtdeclarative,
              kcoreaddons,
              kcrash,
              ki18n,
              kpipewire,
              kstatusnotifieritem,
              kwindowsystem,
              libxcb,
              xcbutil,
            }:
            stdenv.mkDerivation {
              pname = "xwaylandvideobridge";
              version = "0.5.0-kf6";
              src = ./.;
              nativeBuildInputs = [
                cmake
                extra-cmake-modules
                pkg-config
                wrapQtAppsHook
              ];
              buildInputs = [
                qtbase
                qtdeclarative
                kcoreaddons
                kcrash
                ki18n
                kpipewire
                kstatusnotifieritem
                kwindowsystem
                libxcb
                xcbutil
              ];
            }
          ) { };
        });
    };
}
