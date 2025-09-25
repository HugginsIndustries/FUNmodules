#!/bin/bash

# Web GUI Launcher for Scale Converter
# This script opens the web-based GUI in your default browser

echo "🎵 Scale Converter - Web GUI Launcher"
echo "======================================"
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_GUI_FILE="$SCRIPT_DIR/scale_converter_web.html"

# Check if the web GUI file exists
if [ ! -f "$WEB_GUI_FILE" ]; then
    echo "❌ Error: scale_converter_web.html not found!"
    echo "   Make sure you're running this from the helpers directory."
    exit 1
fi

echo "🌐 Opening Scale Converter Web GUI..."
echo "   File: $WEB_GUI_FILE"
echo ""

# Try to open the web GUI in the default browser
if command -v xdg-open >/dev/null 2>&1; then
    # Linux
    xdg-open "$WEB_GUI_FILE"
elif command -v open >/dev/null 2>&1; then
    # macOS
    open "$WEB_GUI_FILE"
elif command -v start >/dev/null 2>&1; then
    # Windows
    start "$WEB_GUI_FILE"
else
    echo "❌ Error: Could not find a command to open the web browser."
    echo "   Please manually open: $WEB_GUI_FILE"
    echo "   Or copy the file path above and paste it into your browser."
    exit 1
fi

echo "✅ Web GUI should now be opening in your browser!"
echo ""
echo "💡 Tips:"
echo "   - If the browser doesn't open automatically, manually open the file"
echo "   - The web GUI works offline and doesn't require internet"
echo "   - You can bookmark this page for easy access"
echo ""
echo "🔧 Alternative usage:"
echo "   - Console version: ./scale_converter"
echo "   - Direct file: open scale_converter_web.html"
