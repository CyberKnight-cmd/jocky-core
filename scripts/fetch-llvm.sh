#!/usr/bin/env bash
# Vendor a pinned LLVM toolchain under third_party/llvm.
#
# The build does NOT require this — it will use any llvm-config >= 23.0 found on
# PATH. Use this only when you want a reproducible, pinned toolchain independent
# of the system LLVM. The download is large (~1 GB compressed, ~12 GB extracted)
# and is git-ignored.
set -euo pipefail
cd "$(dirname "$0")/.."

LLVM_VERSION="${LLVM_VERSION:-23.1.3}"
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) ASSET="LLVM-${LLVM_VERSION}-Linux-X64.tar.xz" ;;
  aarch64|arm64) ASSET="LLVM-${LLVM_VERSION}-Linux-ARM64.tar.xz" ;;
  *) echo "unsupported arch: $ARCH" >&2; exit 1 ;;
esac
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${ASSET}"

mkdir -p third_party
if [ -x third_party/llvm/bin/llvm-config ]; then
  echo "third_party/llvm already present ($(third_party/llvm/bin/llvm-config --version))"
  exit 0
fi

echo "downloading $URL"
curl -sSL -o "third_party/${ASSET}" "$URL"
echo "extracting..."
tar -C third_party -xf "third_party/${ASSET}"
mv "third_party/${ASSET%.tar.xz}" third_party/llvm
rm -f "third_party/${ASSET}"
echo "vendored LLVM $(third_party/llvm/bin/llvm-config --version) at third_party/llvm"
