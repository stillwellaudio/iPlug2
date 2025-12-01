#!/usr/bin/env bash

if [ "$1" == "" ]; then
  if [ "$(uname)" == "Darwin" ]; then
  ZIP_FILE=IPLUG2_DEPS_MAC
  FOLDER=mac
  elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
  FOLDER=linux
  # Linux doesn't have prebuilt dependencies - they must be built from source
  echo "⚠️  Linux detected - prebuilt dependencies are not available."
  echo ""
  echo "You need to build Skia from source. Run the following script:"
  echo "  cd $(dirname $0)/IGraphics && ./build-skia-linux.sh"
  echo ""
  echo "Required system packages (Ubuntu/Debian):"
  echo "  sudo apt-get install build-essential ninja-build python3 git"
  echo "  sudo apt-get install libfontconfig1-dev libfreetype6-dev"
  echo "  sudo apt-get install libx11-dev libxcursor-dev libxrandr-dev libgl1-mesa-dev"
  echo ""
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
elif [ "$1" == "linux" ]; then
  FOLDER=linux
  echo "⚠️  Linux - prebuilt dependencies are not available."
  echo "Run: cd $(dirname $0)/IGraphics && ./build-skia-linux.sh"
  exit 1
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