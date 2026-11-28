#!/usr/bin/env bash
# Fetch the layout-capable CLAP headers without modifying the legacy SDK slots.
set -eu

: "${IPLUG_CLAP_DEPS_DIR:?Set IPLUG_CLAP_DEPS_DIR to a new empty destination directory}"
if [ -e "$IPLUG_CLAP_DEPS_DIR" ] && [ "$(find "$IPLUG_CLAP_DEPS_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  echo "IPLUG_CLAP_DEPS_DIR must be empty: $IPLUG_CLAP_DEPS_DIR" >&2
  exit 1
fi
mkdir -p "$IPLUG_CLAP_DEPS_DIR"
cd "$IPLUG_CLAP_DEPS_DIR"
git clone https://github.com/free-audio/clap.git CLAP_SDK
git -C CLAP_SDK checkout --detach 195b42a004144fab0b3cf95e9c067187d15365b7
git clone https://github.com/free-audio/clap-helpers.git CLAP_HELPERS
git -C CLAP_HELPERS checkout --detach 55a5dd5d1db9c87b32f407e387f64676d27e10b1
