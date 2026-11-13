#!/bin/sh
set -e
cd "$(dirname "$0")"
g++ -O2 -Wall -I.. -o overlay_test overlay.cpp imgui.cpp imgui_draw.cpp \
  imgui_tables.cpp imgui_widgets.cpp imgui_impl_opengl3.cpp \
  $(pkg-config --cflags --libs gtk4 gtk4-layer-shell-0) -lGL
echo "built ./overlay_test"
