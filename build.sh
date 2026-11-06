#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
mkdir -p bin
cc -Wall -Wextra -Werror -Wno-unused-parameter -g -o bin/probe-cycles src/probe.c src/xdg-shell.c src/text-input.c $(pkg-config --cflags --libs wayland-client cairo)
cc -Wall -Wextra -Werror -Wno-unused-parameter -g -o bin/ime-recreate src/ime.c src/xdg-shell.c src/text-input.c src/input-method.c $(pkg-config --cflags --libs wayland-client cairo)
cc -Wall -Wextra -Werror -Wno-unused-parameter -g -o bin/keys src/keys.c src/virtual-keyboard.c $(pkg-config --cflags --libs wayland-client xkbcommon)
