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
"$MACDEPLOYQT" "$APP_PATH" -verbose=1 -always-overwrite 2>&1 || true

# 强制写 qt.conf（macdeployqt 有时遇到 qt.conf 已存在就 skip 写，可能导致插件路径错）
QTCONF="$APP_PATH/Contents/Resources/qt.conf"
if [ ! -f "$QTCONF" ]; then QTCONF="$APP_PATH/Contents/qt.conf"; fi
mkdir -p "$(dirname "$QTCONF")"
cat > "$QTCONF" <<'QTC'
[Paths]
Plugins = PlugIns
Frameworks = Frameworks
QTC
echo "Wrote qt.conf to: $QTCONF"

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
echo "=== Verifying bundle integrity ==="
codesign --verify --deep --strict --verbose=1 "$APP_PATH" 2>&1 | tail -3
echo "  ✅ macdeployqt + codesign done"
echo ""
echo "=== Deployment complete ==="

# ============ 自动创建带版本号后缀的 .app 副本 ============
APP_DIR="$(dirname "$APP_PATH")"
APP_NAME="$(basename "$APP_PATH" .app)"
INFO_PLIST="$APP_PATH/Contents/Info.plist"
if [ -f "$INFO_PLIST" ]; then
    BUNDLE_VER="$(defaults read "$INFO_PLIST" CFBundleShortVersionString 2>/dev/null || true)"
    if [ -n "$BUNDLE_VER" ]; then
        DISPLAY_VER="$(echo "$BUNDLE_VER" | awk -F. '{printf "%d.%02d", $1, $2*10+$3}')"
        VERSIONED_APP="$APP_DIR/${APP_NAME}_v${DISPLAY_VER}.app"
        echo ""
        echo "=== Creating versioned app copy: ${APP_NAME}_v${DISPLAY_VER}.app ==="
        rm -rf "$VERSIONED_APP" 2>/dev/null || true
        cp -R "$APP_PATH" "$VERSIONED_APP"
        codesign --force --deep -s - "$VERSIONED_APP" 2>&1 | tail -2
        xattr -dr com.apple.quarantine "$VERSIONED_APP" 2>/dev/null || true
        codesign --verify --deep --strict "$VERSIONED_APP" 2>&1 | tail -2
        echo "  ✅ Versioned bundle ready: $VERSIONED_APP"
    fi
fi
