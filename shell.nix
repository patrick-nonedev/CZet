# SPDX-License-Identifier: GPL-3.0-or-later
{ pkgs ? import <nixpkgs> {}  }:

pkgs.mkShell
{
    buildInputs =
    [
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
    ];

    shellHook = ''
        export NIX_HARDENING_ENABLE=""
    '';
}
