#!/usr/bin/env bash
# Assemble code/flashfs/ = the SD-card content laid out for the in-flash LittleFS image used by the
# ESP32-WROVER single-slot build (PLAN sec 8). When mounted at /sdcard it must mirror the SD layout:
#   flashfs/html/*    - the web UI (tooltip-generated, $COMMIT_HASH-substituted, gzipped) as on SD
#   flashfs/config/*  - ONLY the 4 best-models (one per family) to fit 4 MB flash (~688 KB vs 3 MB)
# Then `AIOTEDGE_BOARD=BOARD_WROVER_KIT idf.py -B build_wrover build` packs it into the `storage`
# partition (CMakeLists littlefs_create_partition_image) and `... flash` writes it with the app.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
PY="$REPO/.pio-venv/bin/python"; [ -x "$PY" ] || PY=python3
COMMIT="$(git rev-parse --short HEAD)"

# Best-models-only set (recommended/newest per family - see sd-card/html/model_capabilities.html).
BEST_MODELS=(ana-cont_1500_s2_q dig-cont_0900_s3_q dig-class100-0182-s2_q dig-class11_1910_s2_q)

FFS="$REPO/code/flashfs"
rm -rf "$FFS"; mkdir -p "$FFS/html" "$FFS/config"

echo "== Web UI -> flashfs/html (tooltips + commit hash + gzip) =="
( cd tools/parameter-tooltip-generator && "$PY" generate-param-doc-tooltips.py >/dev/null )
cp -r ./sd-card/html/* "$FFS/html/"
rm -f "$FFS/html/edit_config_template.html"   # template not needed on device
find "$FFS/html" -name "*.map" -delete        # JS/CSS source maps are devtools-only, not needed
# Sync version.txt to the firmware's GIT_REV (kept plain - getHTMLversion reads it un-gzipped)
FWREV="$(sed -n 's/.*GIT_REV="\([^"]*\)".*/\1/p' code/main/version.cpp 2>/dev/null || true)"
[ -n "$FWREV" ] && printf 'Development-Branch: %s (Commit: %s)\n%s' "$(git rev-parse --abbrev-ref HEAD)" "$FWREV" "$FWREV" > "$FFS/html/version.txt"
( cd "$FFS/html"
  find . -type f -exec sed -i "s/\$COMMIT_HASH/$COMMIT/g" {} \;
  for ext in html css js jpg png svg map; do find . -name "*.$ext" -type f -exec gzip -f {} \; ; done )

# Deployment manifest for the GUI cleanup feature (see build-release.sh / server_cleanup.cpp).
( cd "$FFS/html" && find . -type f ! -name deployment.lst | sed 's|^\./|html/|' | sort > deployment.lst )

echo "== Best models -> flashfs/config =="
for m in "${BEST_MODELS[@]}"; do
  cp "sd-card/config/${m}.tflite" "$FFS/config/" && echo "   + ${m}.tflite"
done

# Undo the in-place pollution the tooltip generator leaves in the tracked tree
git checkout -q -- sd-card/html/edit_reference.html 2>/dev/null || true
rm -f sd-card/html/edit_config.html
git status --porcelain sd-card/html | awk '$1=="??"{print $2}' | grep -E '\.(png|jpg)$' | xargs -r rm -f

echo "== flashfs assembled: $(du -sh "$FFS" | cut -f1) (storage partition is 1.86 MB) =="
du -sh "$FFS/html" "$FFS/config"
