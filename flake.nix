# SPDX-License-Identifier: GPL-3.0-or-later
# CZet dev shell (replaces shell.nix): toolchain deps to configure and build
# the trimmed GCC tree, plus the static libcs embedded in the blob (musl,
# glibc.static), so `nix develop --command true` also prefetches everything
# `make czet` needs on a fresh machine. Pinned via flake.lock.
{
  description = "CZet self-contained C compiler: dev shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "i686-linux" ];
      eachSystem = nixpkgs.lib.genAttrs systems;
      mkDevShell = pkgs: pkgs.mkShell {
        packages = [
          pkgs.gnumake
          pkgs.gcc
          pkgs.flex
          pkgs.bison
          pkgs.m4
          pkgs.gmp
          pkgs.mpfr
          pkgs.libmpc
          pkgs.zlib
          pkgs.perl
          pkgs.which
          pkgs.coreutils
          pkgs.findutils
          pkgs.gnused
          pkgs.gnugrep
          pkgs.bash
          pkgs.gettext
          pkgs.texinfo
          pkgs.binutils
          # Static libcs embedded in the blob (CI used to nix-build these).
          pkgs.musl
          pkgs.glibc.static
        ];

        shellHook = ''
          export NIX_HARDENING_ENABLE=""
        '';
      };
    in
    {
      devShells = eachSystem (system: {
        default = mkDevShell nixpkgs.legacyPackages.${system};
      });
    };
}
