#!/bin/sh
# Rebuild the patched UI movies from an original resident.ark and write the
# SSDF diffs the installer applies (patch/*.ssdf).
#   tools/make_diffs.sh <original resident.ark> <ffdec.jar>
# Needs python3 (no extra modules) and Java for JPEXS FFDec.
set -eu
ARK=$1
FFDEC=$2
PY=${PYTHON:-python3}
W=build/ui
mkdir -p "$W" patch
ffdec() { java -jar "$FFDEC" "$@" 2>&1 | grep -v -e EXPERIMENTAL -e 'Replace AS1/2' || true; }

# UI/CommonUI/Prompt_Bar.gfx: per-button icon sprites showing Xbox glyphs
$PY tools/ark.py extract "$ARK" 266e5f18 "$W/prompt_bar.orig.gfx"
$PY tools/padicons.py "$W/prompt_bar.orig.gfx" "$W/prompt_bar.stage.gfx"
# icon sprite ids are assigned from 900 by padicons.py: 901, 904, 907, 910, 913
ffdec -replace "$W/prompt_bar.stage.gfx" "$W/prompt_bar.gfx" \
    /DefineSprite_901/frame_1/DoAction ui/padicon_frame1.as \
    /DefineSprite_904/frame_1/DoAction ui/padicon_frame1.as \
    /DefineSprite_907/frame_1/DoAction ui/padicon_frame1.as \
    /DefineSprite_910/frame_1/DoAction ui/padicon_frame1.as \
    /DefineSprite_913/frame_1/DoAction ui/padicon_frame1.as

# UI/Frontend/Flash/FE_pressStart_Overlay.gfx: A button instead of "ENTER"
$PY tools/ark.py extract "$ARK" 51bf9dda "$W/press_start.orig.gfx"
$PY tools/pressstart.py "$W/press_start.orig.gfx" "$W/prompt_bar.orig.gfx" "$W/press_start.stage.gfx"
ffdec -replace "$W/press_start.stage.gfx" "$W/press_start.gfx" \
    /DefineSprite_3/frame_1/DoAction ui/pressstart_text.as

$PY tools/mkdiff.py 266e5f18 "$W/prompt_bar.orig.gfx" "$W/prompt_bar.gfx" patch/prompt_bar.ssdf
$PY tools/mkdiff.py 51bf9dda "$W/press_start.orig.gfx" "$W/press_start.gfx" patch/press_start.ssdf
