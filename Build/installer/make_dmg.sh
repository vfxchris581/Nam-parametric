#!/usr/bin/env bash
# Builds the macOS disk image: the app, the pnam command-line tool and an Applications link.
#   make_dmg.sh "<path to .app>" "<path to pnam>" <output.dmg>
set -euo pipefail
APP="$1"
CLI="$2"
OUT="$3"
STAGE="$(mktemp -d)/Parametric NAM Studio"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
mkdir -p "$STAGE/Command line tool"
cp "$CLI" "$STAGE/Command line tool/pnam"
ln -s /Applications "$STAGE/Applications"
cat > "$STAGE/READ ME.txt" <<'EOF'
Parametric NAM Studio

Drag "Parametric NAM Studio" onto Applications.

If macOS says the app cannot be opened because the developer cannot be verified
(builds without a Developer ID signature), right-click the app, choose Open, then Open.

The "Command line tool" folder contains `pnam` (train, render, snapshot, analyze).
EOF
rm -f "$OUT"
hdiutil create -volname "Parametric NAM Studio" -srcfolder "$STAGE" -ov -format UDZO "$OUT"
echo "wrote $OUT"
