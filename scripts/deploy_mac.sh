#!/bin/bash
set -e

APP_PATH="$1"

if [ -z "$APP_PATH" ]; then
    echo "Usage: $0 <path_to.app>"
    exit 1
fi

if [ ! -d "$APP_PATH" ]; then
    echo "Error: $APP_PATH not found"
    exit 1
fi

echo "=== Deploying Qt frameworks for $APP_PATH ==="

MACDEPLOYQT=$(which macdeployqt 2>/dev/null || true)
if [ -z "$MACDEPLOYQT" ]; then
    MACDEPLOYQT="/opt/homebrew/bin/macdeployqt"
fi

if [ ! -f "$MACDEPLOYQT" ]; then
    echo "Warning: macdeployqt not found, skipping deployment"
    exit 0
fi

echo "Running macdeployqt..."
"$MACDEPLOYQT" "$APP_PATH" -verbose=1 2>&1 || true

echo ""
echo "=== Fixing code signatures ==="

echo "Removing existing signatures from dylibs..."
find "$APP_PATH/Contents/Frameworks" -name "*.dylib" -exec codesign --remove-signature {} \; 2>/dev/null || true
find "$APP_PATH/Contents/PlugIns" -name "*.dylib" -exec codesign --remove-signature {} \; 2>/dev/null || true

echo "Re-signing with ad-hoc signature..."
codesign --force --deep -s - "$APP_PATH" 2>&1

echo ""
echo "=== Removing quarantine attribute ==="
xattr -dr com.apple.quarantine "$APP_PATH" 2>/dev/null || true

echo ""
echo "=== Deployment complete ==="
