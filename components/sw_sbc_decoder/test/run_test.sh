#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
COMP_DIR="$DIR/.."
BUILD_DIR="$DIR/build"
mkdir -p "$BUILD_DIR"

echo "=== Compiling test_decode ==="
gcc -O2 -Wall -Wextra -Wno-override-init \
    -I"$COMP_DIR/include" -I"$COMP_DIR/src" \
    "$DIR/test_decode.c" "$COMP_DIR/src/sbc.c" "$COMP_DIR/src/bits.c" \
    -lm -o "$BUILD_DIR/test_decode"
echo "  -> $BUILD_DIR/test_decode"

echo ""
echo "=== Generating test SBC data with ffmpeg ==="

ffmpeg -y -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" \
       -f lavfi -i "sine=frequency=880:duration=2:sample_rate=44100" \
       -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" \
       -map "[a]" \
       -f s16le -acodec pcm_s16le \
       "$BUILD_DIR/ref_source.pcm" 2>/dev/null
echo "  -> ref_source.pcm"

ffmpeg -y -f s16le -ar 44100 -ac 2 -i "$BUILD_DIR/ref_source.pcm" \
       -acodec sbc -ar 44100 -ac 2 \
       "$BUILD_DIR/test_input.sbc" 2>/dev/null
echo "  -> test_input.sbc"

ffmpeg -y -i "$BUILD_DIR/test_input.sbc" \
       -f s16le -acodec pcm_s16le \
       "$BUILD_DIR/ref_decoded.pcm" 2>/dev/null
echo "  -> ref_decoded.pcm"

echo ""
echo "=== Running decoder test ==="
cd "$BUILD_DIR"
./test_decode test_input.sbc ref_decoded.pcm
