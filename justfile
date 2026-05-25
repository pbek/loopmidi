# Build and run commands for loopmidi

import ".shared/common.just"
import ".shared/cpp.just"

# Default recipe - show available commands
default:
    @just --list

# Variables

transferDir := `if [ -d "$HOME/NextcloudPrivate/Transfer" ]; then echo "$HOME/NextcloudPrivate/Transfer"; else echo "$HOME/Nextcloud/Transfer"; fi`

# Build the Nix package using flakes (if available)
nix-build:
    nix build .#loopmidi

# Run the Nix package using flakes (if available)
nix-run:
    nix run .#loopmidi

# Apply a git patch to the project
[group('patches')]
git-apply-patch:
    git apply {{ transferDir }}/loopmidi.patch

# Create git patches for the project
[group('patches')]
git-create-patch:
    @echo "transferDir: {{ transferDir }}"
    git diff --no-ext-diff --staged --binary > {{ transferDir }}/loopmidi.patch
    ls -l1t {{ transferDir }} | head -2
