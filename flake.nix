{
  description = "OpenQuick C23 CLI development environment";

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
          pkgs = nixpkgs.legacyPackages.${system};
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

            # Project setup/cleanup scripts.
            pkgs.python3
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
            name = "openquick";

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

      packages = eachSystem (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          lib = pkgs.lib;
          zig = pkgs.zig_0_16 or pkgs.zig;

          quick = pkgs.stdenv.mkDerivation {
            pname = "openquick";
            version = "0.1.0";
            src = ./.;

            strictDeps = true;
            dontConfigure = true;

            nativeBuildInputs = [
              pkgs.makeWrapper
              zig
            ];

            buildPhase = ''
              runHook preBuild

              export HOME="$TMPDIR"
              export ZIG_GLOBAL_CACHE_DIR="$TMPDIR/zig-global-cache"
              export ZIG_LOCAL_CACHE_DIR="$TMPDIR/zig-local-cache"

              zig build \
                -Doptimize=ReleaseSafe \
                -Denable-tui=false \
                -Dcli-terminfo=disabled \
                -Dstrip=true

              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall

              install -Dm755 zig-out/bin/quick "$out/bin/quick"

              mkdir -p "$out/share/agent-skills"
              cp -R skills/openquick-deploy "$out/share/agent-skills/openquick"
              chmod -R u+w,go-w "$out/share/agent-skills/openquick"

              wrapProgram "$out/bin/quick" \
                --prefix PATH : ${
                  lib.makeBinPath (
                    [
                      pkgs.openssh
                      pkgs.rsync
                    ]
                    ++ lib.optionals pkgs.stdenv.isLinux [ pkgs.xdg-utils ]
                  )
                }

              runHook postInstall
            '';

            doInstallCheck = true;
            installCheckPhase = ''
              runHook preInstallCheck

              "$out/bin/quick" --version >/dev/null
              test -f "$out/share/agent-skills/openquick/SKILL.md"

              runHook postInstallCheck
            '';

            meta = {
              description = "Deploy a folder to a private OpenQuick URL via rsync and quickd";
              mainProgram = "quick";
              license = lib.licenses.mit;
            };
          };

          quickd = pkgs.buildGoModule {
            pname = "quickd";
            version = "0.1.0";
            src = ./server;

            vendorHash = "sha256-e0+hudSZ3lc5sJv1xTF2BT7EpGt20ZAVryaWkDPEj3A=";

            # Darwin sandbox makes modernc/libc netdb panic opening /etc/protocols; Go tests run in CI/dev workflows.
            doCheck = false;

            env.CGO_ENABLED = "0";
            subPackages = [ "cmd/quickd" ];

            meta = {
              description = "OpenQuick host daemon: release activation, identity-aware static serving, and the same-origin site API";
              mainProgram = "quickd";
              license = lib.licenses.mit;
            };
          };
        in
        {
          inherit quick quickd;
          default = quick;
        }
      );

      formatter = eachSystem (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
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
