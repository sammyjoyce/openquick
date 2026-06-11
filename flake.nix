{
  description = "C23 CLI template development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      eachSystem = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = eachSystem (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          lib = pkgs.lib;
          ncursesDev = lib.getDev pkgs.ncurses;
          ncursesLib = lib.getLib pkgs.ncurses;
          libcDev = lib.getDev pkgs.stdenv.cc.libc;
          ghosttyVt = pkgs.libghostty-vt;
          ghosttyVtDev = lib.getDev ghosttyVt;
          ghosttyVtLib = lib.getLib ghosttyVt;
          zig = pkgs.zig_0_16 or pkgs.zig;
          projectTooling = [
            # Build and day-to-day workflow.
            zig
            pkgs.pkg-config
            pkgs.git
            pkgs.gh
            pkgs.just
            pkgs.bashInteractive
            pkgs.coreutils
            pkgs.findutils
            pkgs.gawk
            pkgs.gnugrep
            pkgs.gnused
            pkgs.gnutar
            pkgs.xz
            pkgs.curl

            # C/Zig editing, debugging, and CI lint parity.
            pkgs.clang
            pkgs.clang-tools
            pkgs.cmake
            pkgs.ninja
            pkgs.lldb
            pkgs.cppcheck
            pkgs.shellcheck
            pkgs.nixfmt

            # Documentation lint tooling.
            pkgs.nodejs
            pkgs.markdownlint-cli

            # Template setup/cleanup scripts.
            pkgs.jq
            pkgs.sd
            pkgs.gum

            # Demo, security, and release-support tooling used by repo scripts/CI.
            pkgs.asciinema
            pkgs.asciinema-agg
            pkgs.gitleaks
            pkgs.syft
          ]
          ++ lib.optionals pkgs.stdenv.isLinux [
            pkgs.gdb
            pkgs.valgrind
          ];
        in
        {
          default = pkgs.mkShell {
            name = "curspan";

            packages = projectTooling;

            buildInputs = [
              ncursesDev
              ncursesLib
              ghosttyVtDev
              ghosttyVtLib
            ];

            # Zig's linkSystemLibrary(...) consults pkg-config first.
            # Keep ncurses and libghostty-vt explicit so `zig build run`
            # and `zig build terminal-test` work on NixOS without extra flags.
            PKG_CONFIG_PATH = lib.concatStringsSep ":" [
              "${ncursesDev}/lib/pkgconfig"
              "${ghosttyVtDev}/share/pkgconfig"
            ];
            CPATH = lib.concatStringsSep ":" [
              "${libcDev}/include"
              "${ncursesDev}/include"
              "${ghosttyVtDev}/include"
            ];
            LIBRARY_PATH = lib.concatStringsSep ":" [
              "${ncursesLib}/lib"
              "${ghosttyVtLib}/lib"
            ];
            LD_LIBRARY_PATH = lib.optionalString pkgs.stdenv.isLinux (
              lib.concatStringsSep ":" [
                "${ncursesLib}/lib"
                "${ghosttyVtLib}/lib"
              ]
            );
            DYLD_FALLBACK_LIBRARY_PATH = lib.optionalString pkgs.stdenv.isDarwin (
              lib.concatStringsSep ":" [
                "${ncursesLib}/lib"
                "${ghosttyVtLib}/lib"
              ]
            );
            TERMINFO_DIRS = "${ncursesLib}/share/terminfo";
          };
        }
      );

      formatter = eachSystem (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        pkgs.writeShellScriptBin "format-nix" ''
          if [ "$#" -eq 0 ]; then
            set -- flake.nix
          fi

          exec ${pkgs.nixfmt}/bin/nixfmt "$@"
        ''
      );
    };
}
