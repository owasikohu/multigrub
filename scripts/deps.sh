#!/usr/bin/env bash
# Install development tools locally using signed Debian package indices.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/.build
mkdir -p "$BUILD/apt/lists/partial" "$BUILD/apt/cache/archives/partial" "$BUILD/tools"
if ! grep -q '^VERSION_CODENAME=trixie$' /etc/os-release; then
  echo 'Local dependency provisioning currently requires Debian trixie.' >&2
  echo 'On other hosts install the packages listed in README.md using your package manager.' >&2
  exit 1
fi
cat > "$BUILD/apt/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
SOURCES
cat > "$BUILD/apt/config" <<CONFIG
Dir::Etc::parts "$BUILD/apt/empty";
Dir::Etc::main "$BUILD/apt/empty.conf";
Dir::Etc::sourcelist "$BUILD/apt/sources.list";
Dir::Etc::sourceparts "$BUILD/apt/empty";
Dir::State::lists "$BUILD/apt/lists";
Dir::Cache "$BUILD/apt/cache";
APT::Sandbox::User "$(id -un)";
CONFIG
mkdir -p "$BUILD/apt/empty"
touch "$BUILD/apt/empty.conf"
export APT_CONFIG="$BUILD/apt/config"
/usr/bin/apt-get update
/usr/bin/apt-get -o Debug::NoLocking=1 --download-only --no-install-recommends -y install \
  qemu-system-x86 ovmf xorriso mtools gdisk dosfstools e2fsprogs bison flex gawk gnu-efi tesseract-ocr tesseract-ocr-eng
for pkg in "$BUILD/apt/cache/archives/"*.deb; do
  dpkg-deb -x "$pkg" "$BUILD/tools"
done
