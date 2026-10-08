#!/usr/bin/env bash
# Source from scripts; all paths are absolute and local installation is optional.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=$ROOT/.build
TOOLS=$BUILD/tools
export PATH="$TOOLS/usr/bin:$TOOLS/usr/sbin:$TOOLS/bin:$TOOLS/sbin:$PATH"
export LD_LIBRARY_PATH="$TOOLS/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ -d $TOOLS/usr/share/bison ]]; then
  export BISON_PKGDATADIR="$TOOLS/usr/share/bison"
fi
if [[ -f $TOOLS/usr/share/tesseract-ocr/5/tessdata/eng.traineddata ]]; then
  export TESSDATA_PREFIX="$TOOLS/usr/share/tesseract-ocr/5/tessdata"
fi
export M4=m4
