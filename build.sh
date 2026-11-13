#!/usr/bin/env bash
set -Eeuo pipefail

cd "$(dirname "$0")"

mkdir -p build

sources=(
    src/main.cpp
    src/config.cpp
    src/rtti.cpp
    src/roblox.cpp
    src/scanner.cpp
    src/dump/static.cpp
    src/dump/general.cpp
    src/dump/datamodel.cpp
    src/dump/players.cpp
    src/dump/camera.cpp
    src/dump/humanoid.cpp
    src/dump/parts.cpp
    src/dump/world.cpp
    src/dump/mouse.cpp
    src/dump/workspace.cpp
    src/dump/animation.cpp
    src/dump/lighting.cpp
    src/dump/interactables.cpp
    src/dump/luau.cpp
    src/dump/flags.cpp
    src/dump/diag.cpp
)

objects=()
total=${#sources[@]}
count=0

for source in "${sources[@]}"; do
    count=$((count + 1))
    object="build/${source//\//_}.o"

    echo "[$count/$total] Compiling $source"

    g++ -O2 -Wall \
        -Isrc \
        -I/home/idom/int3-kernel/include \
        -c "$source" \
        -o "$object"

    objects+=("$object")
done

echo "[LINK] Linking executable..."

g++ "${objects[@]}" -o lx_dump

echo "[SUCCESS] Built ./lx_dump"
