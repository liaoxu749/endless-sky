#!/bin/sh
# Copies the SDL2 Android Java glue (SDLActivity & friends) out of an SDL2
# release. The rest of the android-project is committed in this repository;
# only these files come from SDL to keep them in sync with the SDL version
# the native build uses.
# Usage: fetch-sdl-java.sh <SDL-version, e.g. 2.30.8> <target java dir>
set -e
SDL_VERSION="$1"
OUT="$2"
TARBALL="SDL2-${SDL_VERSION}.zip"
URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/${TARBALL}"

echo "Fetching ${URL}"
curl -fL "${URL}" -o "${TARBALL}"
unzip -q -o "${TARBALL}" \
	"SDL2-${SDL_VERSION}/android-project/app/src/main/java/org/libsdl/app/*"
mkdir -p "${OUT}/org/libsdl/app"
cp SDL2-${SDL_VERSION}/android-project/app/src/main/java/org/libsdl/app/*.java \
	"${OUT}/org/libsdl/app/"
rm -rf "SDL2-${SDL_VERSION}" "${TARBALL}"
echo "SDL Java glue installed into ${OUT}/org/libsdl/app/"
