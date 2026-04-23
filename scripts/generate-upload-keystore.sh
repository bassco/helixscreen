#!/usr/bin/env bash
# Generate an upload keystore for Google Play Store signing.
# The keystore is stored OUTSIDE the repo (never committed).
#
# Local builds read the keystore path from ANDROID_KEYSTORE_PATH.
# CI decodes ANDROID_KEYSTORE_BASE64 into a temporary file at runtime
# (see .github/workflows/release.yml "Materialize upload keystore" step).

set -euo pipefail

KEYSTORE_DIR="${HOME}/.android-keystore"
KEYSTORE_PATH="${KEYSTORE_DIR}/helixscreen-upload.jks"
KEY_ALIAS="helixscreen-upload"

if [ -f "$KEYSTORE_PATH" ]; then
    echo "Keystore already exists at: $KEYSTORE_PATH"
    echo "Delete it first if you want to regenerate."
    exit 1
fi

mkdir -p "$KEYSTORE_DIR"

echo "Generating upload keystore for HelixScreen..."
echo "Store path: $KEYSTORE_PATH"
echo "Key alias: $KEY_ALIAS"
echo ""
echo "You will be prompted for passwords and certificate info."
echo ""

keytool -genkeypair \
    -v \
    -keystore "$KEYSTORE_PATH" \
    -alias "$KEY_ALIAS" \
    -keyalg RSA \
    -keysize 2048 \
    -validity 10000

echo ""
echo "Keystore generated at: $KEYSTORE_PATH"
echo ""
echo "For local builds, set these environment variables:"
echo "  export ANDROID_KEYSTORE_PATH=$KEYSTORE_PATH"
echo "  export ANDROID_KEY_ALIAS=$KEY_ALIAS"
echo "  export ANDROID_KEYSTORE_PASSWORD=<your store password>"
echo "  export ANDROID_KEY_PASSWORD=<your key password>"
echo ""
echo "For CI (GitHub Actions), add these 4 repository secrets:"
echo "  ANDROID_KEYSTORE_BASE64  (paste the output of: base64 -w0 $KEYSTORE_PATH)"
echo "  ANDROID_KEY_ALIAS        ($KEY_ALIAS)"
echo "  ANDROID_KEYSTORE_PASSWORD"
echo "  ANDROID_KEY_PASSWORD"
echo ""
echo "To generate the base64-encoded keystore for the secret, run:"
echo "  base64 -w0 $KEYSTORE_PATH | pbcopy    # macOS (copies to clipboard)"
echo "  base64 -w0 $KEYSTORE_PATH | wl-copy   # Linux / Wayland"
echo "  base64 -w0 $KEYSTORE_PATH > /tmp/keystore.b64   # anywhere (then cat + copy)"
