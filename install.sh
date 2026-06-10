#!/bin/sh
# brokm installer / updater. Safe to re-run; each run brings the install
# up to date.
#
#   sh install.sh             install (or update) into ~/.brokm
#   BROKM_HOME=/opt/brokm sh install.sh    ...or anywhere else
#
# The install directory IS a git clone of the repository - binary, standard
# library (lib/), runtime sources (src/, needed by `brokm build`), and docs
# all live in the one place. Updating is the same command: the script pulls
# and rebuilds. Uninstalling is `rm -rf ~/.brokm`.
set -e

REPO_URL="${BROKM_REPO:-https://github.com/antomfdez/brokm.git}"
HOME_DIR="${BROKM_HOME:-$HOME/.brokm}"

if [ -d "$HOME_DIR/.git" ]; then
  echo "brokm: updating $HOME_DIR"
  git -C "$HOME_DIR" pull --ff-only
elif [ -e "$HOME_DIR" ]; then
  echo "brokm: $HOME_DIR exists but is not a git clone - refusing to touch it." >&2
  echo "       Move it aside or set BROKM_HOME elsewhere." >&2
  exit 1
else
  echo "brokm: cloning into $HOME_DIR"
  git clone "$REPO_URL" "$HOME_DIR"
fi

echo "brokm: building"
make -C "$HOME_DIR" --no-print-directory

"$HOME_DIR/brokm" --version

# Done. Tell the user the two lines their shell profile needs, but only if
# they are not already in effect.
case ":$PATH:" in
  *":$HOME_DIR:"*) ;;
  *)
    echo ""
    echo "Add brokm to your shell profile:"
    echo ""
    echo "  # bash/zsh (~/.bashrc, ~/.zshrc)"
    echo "  export BROKM_HOME=\"$HOME_DIR\""
    echo "  export PATH=\"\$BROKM_HOME:\$PATH\""
    echo ""
    echo "  # fish (~/.config/fish/config.fish)"
    echo "  set -gx BROKM_HOME $HOME_DIR"
    echo "  fish_add_path $HOME_DIR"
    ;;
esac

echo ""
echo "brokm: done. Scripts can now use:  #include \"std/std.bk\""
echo "brokm: update any time by re-running this script."
