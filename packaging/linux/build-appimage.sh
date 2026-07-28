#!/bin/sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CONFIG="${1:-Release}"
case "$CONFIG" in
    debug|Debug)     CONFIG=Debug ;;
    release|Release) CONFIG=Release ;;
    dist|Dist)       CONFIG=Dist ;;
    *)
        echo "Unknown config: $CONFIG"
        echo "Usage: $0 [debug|release|dist] [editor|runtime]"
        exit 1
        ;;
esac

TARGET="${2:-editor}"
case "$TARGET" in
    editor|Editor)   TARGET=editor ;;
    runtime|Runtime) TARGET=runtime ;;
    *)
        echo "Unknown target: $TARGET"
        echo "Usage: $0 [debug|release|dist] [editor|runtime]"
        exit 1
        ;;
esac

CONFIG_LOWER=$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')
BIN_DIR="$REPO_ROOT/bin/$CONFIG-linux-x86_64"

if [ "$TARGET" = "editor" ]; then
    APP_NAME="LuxEditor"
    SOURCE_BIN="$BIN_DIR/Editor/Editor"
    DESKTOP_FILE="$SCRIPT_DIR/lux-editor.desktop"
else
    APP_NAME="LuxRuntime"
    SOURCE_BIN="$BIN_DIR/Lux-Runtime/Lux-Runtime"
    DESKTOP_FILE="" # generated below
fi

if [ ! -f "$SOURCE_BIN" ]; then
    echo "Binary not found: $SOURCE_BIN"
    echo "Build with: make config=$CONFIG_LOWER $( [ "$TARGET" = "editor" ] && echo "Editor" || echo "Lux-Runtime" )"
    exit 1
fi

APPDIR="$REPO_ROOT/packaging/linux/$APP_NAME.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$SOURCE_BIN" "$APPDIR/usr/bin/"

VULKAN_SDK="$REPO_ROOT/Core/vendor/VulkanSDK/x86_64"
for lib in "$VULKAN_SDK/lib/"*.so*; do
    [ -f "$lib" ] && cp -P "$lib" "$APPDIR/usr/lib/"
done

for lib in "$REPO_ROOT/Core/vendor/assimp/bin/linux/"*.so*; do
    [ -f "$lib" ] && cp -P "$lib" "$APPDIR/usr/lib/"
done

if [ -d "$REPO_ROOT/Core/vendor/NvidiaAftermath/lib/x64/linux" ]; then
    for lib in "$REPO_ROOT/Core/vendor/NvidiaAftermath/lib/x64/linux/"*.so*; do
        [ -f "$lib" ] && cp -P "$lib" "$APPDIR/usr/lib/"
    done
fi

if [ "$TARGET" = "editor" ]; then
    cp -r "$BIN_DIR/Editor/Resources" "$APPDIR/usr/bin/Resources" 2>/dev/null || \
        cp -r "$REPO_ROOT/Editor/Resources" "$APPDIR/usr/bin/Resources"
    [ -d "$BIN_DIR/Editor/DotNet" ] && cp -r "$BIN_DIR/Editor/DotNet" "$APPDIR/usr/bin/DotNet"
    [ -d "$REPO_ROOT/Editor/DotNet" ] && [ ! -d "$APPDIR/usr/bin/DotNet" ] && \
        cp -r "$REPO_ROOT/Editor/DotNet" "$APPDIR/usr/bin/DotNet"
else
    cp -r "$BIN_DIR/Lux-Runtime/Resources" "$APPDIR/usr/bin/Resources" 2>/dev/null || \
        cp -r "$REPO_ROOT/Editor/Resources" "$APPDIR/usr/bin/Resources"
    [ -d "$BIN_DIR/Lux-Runtime/DotNet" ] && cp -r "$BIN_DIR/Lux-Runtime/DotNet" "$APPDIR/usr/bin/DotNet"
    [ -d "$REPO_ROOT/Editor/DotNet" ] && [ ! -d "$APPDIR/usr/bin/DotNet" ] && \
        cp -r "$REPO_ROOT/Editor/DotNet" "$APPDIR/usr/bin/DotNet"
fi

if [ -d "$VULKAN_SDK/bin" ]; then
    cp "$VULKAN_SDK/bin/dxc" "$APPDIR/usr/bin/" 2>/dev/null || true
    cp "$VULKAN_SDK/bin/dxc-"* "$APPDIR/usr/bin/" 2>/dev/null || true
fi

if [ "$TARGET" = "editor" ]; then
    EXEC_NAME="Editor"
    cp "$DESKTOP_FILE" "$APPDIR/usr/share/applications/"
else
    EXEC_NAME="Lux-Runtime"
    cat > "$APPDIR/usr/share/applications/lux-runtime.desktop" << 'DESKTOP'
[Desktop Entry]
Type=Application
Name=Lux Runtime
Comment=Lux Engine Runtime Player
Exec=Lux-Runtime
Icon=lux-runtime
Terminal=false
Categories=Game;
DESKTOP
fi

# Placeholder icon (1x1 PNG)
if [ ! -f "$APPDIR/usr/share/icons/hicolor/256x256/apps/lux-${TARGET}.png" ]; then
    printf '\x89PNG\r\n\x1a\n' > "$APPDIR/usr/share/icons/hicolor/256x256/apps/lux-${TARGET}.png"
fi

cat > "$APPDIR/AppRun" << APPRUN
#!/bin/sh
SELF="\$(readlink -f "\$0")"
HERE="\${SELF%/*}"
export LD_LIBRARY_PATH="\$HERE/usr/lib:\$LD_LIBRARY_PATH"
export PATH="\$HERE/usr/bin:\$PATH"
exec "\$HERE/usr/bin/$EXEC_NAME" "\$@"
APPRUN
chmod +x "$APPDIR/AppRun"

ln -sf "usr/share/applications/lux-${TARGET}.desktop" "$APPDIR/lux-${TARGET}.desktop"
ln -sf "usr/share/icons/hicolor/256x256/apps/lux-${TARGET}.png" "$APPDIR/.DirIcon"

APPIMAGETOOL="$(command -v appimagetool 2>/dev/null || echo "")"
if [ -z "$APPIMAGETOOL" ]; then
    echo "appimagetool not found. AppDir created at: $APPDIR"
    echo "Install appimagetool and run: appimagetool \"$APPDIR\""
    exit 0
fi

OUTPUT="$REPO_ROOT/packaging/linux/$APP_NAME-x86_64.AppImage"
"$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
echo "AppImage created: $OUTPUT"
