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
        version = "0.2.0";

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

          postInstall = ''
            # Install icon for window managers / taskbars
            install -Dm644 $src/icons/app.png \
              $out/share/icons/hicolor/256x256/apps/loopmidi.png

            # Install .desktop file so app launchers show the icon
            mkdir -p $out/share/applications
            cat > $out/share/applications/loopmidi.desktop << EOF
[Desktop Entry]
Name=LoopMidi
Comment=MIDI Loop Sequencer
Exec=loopmidi
Icon=loopmidi
Type=Application
Categories=Audio;Music;
EOF
          '';

          # - Prepend our share/ dir so the .desktop file is findable
          # - Unset QT_QPA_PLATFORMTHEME so Qt skips the xdg-desktop-portal
          #   icon lookup (which fails without a system-level .desktop install)
          #   and instead sets _NET_WM_ICON directly from the embedded resource.
          qtWrapperArgs = [
            "--prefix" "XDG_DATA_DIRS" ":" "$out/share"
            "--unset" "QT_QPA_PLATFORMTHEME"
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
