#!/usr/bin/env bash
# Regenerate the screenshots of the calibration guide from the radio simulator.
#
#   code/docs/CalibrationGuide/make_screenshots.sh [build directory]
#
# Builds calguide_screenshots and autocal_screenshots (SDL display, offscreen),
# runs them, and writes the PNGs the guide uses into this folder. The default
# build directory is code/test/build (the unit tests' build, with the
# simulator switched on). Needs SDL2, CMake and ffmpeg.
# The dual-VFO run includes both automatic IQ calibrations in real time, so the
# whole script takes several minutes.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tests="$(cd "$here/../../test" && pwd)"
build="${1:-$tests/build}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$build"
cmake -S "$tests" -B "$build" -DUSE_SDL_DISPLAY=ON > /dev/null
cmake --build "$build" --target calguide_screenshots autocal_screenshots -j"$(nproc)"

export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
(cd "$build" && ./calguide_screenshots single "$work/guide")
(cd "$build" && ./calguide_screenshots dual "$work/guide")
(cd "$build" && ./autocal_screenshots 1 "$work/auto")
(cd "$build" && ./autocal_screenshots 2 "$work/auto")

# <run>-<nn>-<name>.bmp -> <name>.png, with the VFO type in the name where both
# runs take the same screen
convert() { ffmpeg -loglevel error -y -i "$1" "$here/$2"; }
for f in "$work"/guide/*.bmp; do
    base="$(basename "$f" .bmp)"
    run="${base%%-*}"
    name="${base#*-*-}"
    case "$name" in
        bit|txiq-*) convert "$f" "$name-$run.png" ;;
        *)          convert "$f" "$name.png" ;;
    esac
done
# L<level>-<nn>-<name>.bmp -> Auto-L<level>-<nn>-<name>.png, as the power
# calibration guide in code/docs/PowerCalibration names them
for f in "$work"/auto/*.bmp; do
    convert "$f" "Auto-$(basename "$f" .bmp).png"
done
# Keep only the images the guide uses
for f in "$here"/*.png; do
    grep -q "src=\"$(basename "$f")\"" "$here/Calibration_Guide.html" || rm "$f"
done
echo "Screenshots written to $here"
