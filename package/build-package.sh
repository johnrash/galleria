#!/bin/bash
# Builds Galleria and its .hpkg package. Runs on Haiku (needs haiku_devel):
#   bash package/build-package.sh
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
NAME=galleria
VERSION=$(grep '^version' package/PackageInfo | awk '{print $2}')
ARCH=$(getarch)

make

PKG=$ROOT/package/pkg
rm -rf "$PKG"
mkdir -p "$PKG/apps" "$PKG/data/deskbar/menu/Applications" \
	"$PKG/documentation/packages/$NAME"

cp objects.*/Galleria "$PKG/apps/Galleria"
# Deskbar menu entry
ln -s ../../../../apps/Galleria \
	"$PKG/data/deskbar/menu/Applications/Galleria"
cp README.md LICENSE "$PKG/documentation/packages/$NAME/"
sed "s/^architecture.*/architecture\t$ARCH/" package/PackageInfo \
	> "$PKG/.PackageInfo"

OUT=$ROOT/package/$NAME-$VERSION-$ARCH.hpkg
rm -f "$OUT"
(cd "$PKG" && package create -q "$OUT")
rm -rf "$PKG"
echo "Created $OUT"
echo "Install with: pkgman install $OUT"
