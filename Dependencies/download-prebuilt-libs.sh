#!/usr/bin/env bash

if [ "$1" == "" ]; then
  if [ "$(uname)" == "Darwin" ]; then
  ZIP_FILE=IPLUG2_DEPS_MAC
  FOLDER=mac
  elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
  exit 1
  else
  ZIP_FILE=IPLUG2_DEPS_WIN
  FOLDER=win
  fi
elif [ "$1" == "mac" ]; then
  ZIP_FILE=IPLUG2_DEPS_MAC
  FOLDER=mac
elif [ "$1" == "ios" ]; then
  ZIP_FILE=IPLUG2_DEPS_IOS
  FOLDER=ios
elif [ "$1" == "win" ]; then
  ZIP_FILE=IPLUG2_DEPS_WIN
  FOLDER=win
fi


# Only download if prebuilt files aren't already present
if [ ! -d Build/$FOLDER ] || [ ! -d Build/src ]; then
  echo "🔽 Prebuilt dependencies not found — downloading $ZIP_FILE.zip..."
  curl https://github.com/iPlug2/iPlug2/releases/download/v1.0.0-beta/$ZIP_FILE.zip -L -J -O

  mkdir -p Build

  unzip -o $ZIP_FILE.zip
  mv $ZIP_FILE/* Build

  rm -r $ZIP_FILE
  rm -f $ZIP_FILE.zip
else
  echo "✅ Prebuilt dependencies found in cache — skipping download."
fi