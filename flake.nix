{
  description = "LoopMidi — Qt/QML MIDI loop sequencer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # ── Single source of truth for the version ──────────────────────────
        version = "0.1.0";

        loopmidi = pkgs.stdenv.mkDerivation {
          pname   = "loopmidi";
          inherit version;

          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtdeclarative   # Qt Quick / QML
            rtmidi
            alsa-lib
          ];

          cmakeFlags = [
            "-DAPP_VERSION=${version}"
          ];

          meta = with pkgs.lib; {
            description = "MIDI loop sequencer: record 16 notes, loop them as a virtual MIDI device";
            homepage    = "https://github.com/yourusername/loopmidi";
            license     = licenses.gpl3Only;
            platforms   = platforms.linux;
            mainProgram = "loopmidi";
          };
        };
      in
      {
        packages = {
          default  = loopmidi;
          loopmidi = loopmidi;
        };

        apps.default = flake-utils.lib.mkApp {
          drv  = loopmidi;
          name = "loopmidi";
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            pkg-config
            qt6.qtbase
            qt6.qtdeclarative
            qt6.qttools
            rtmidi
            alsa-lib
            gdb
            clang-tools   # clangd LSP
          ];

          shellHook = ''
            export QT_QPA_PLATFORM=xcb
            echo "LoopMidi dev shell — version ${version}"
          '';
        };
      });
}
