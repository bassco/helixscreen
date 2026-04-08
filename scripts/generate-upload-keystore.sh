#!/usr/bin/env bash
# Generate an upload keystore for Google Play Store signing.
# The keystore is stored OUTSIDE the repo (never committed).
# Set these env vars for CI/local builds:
#   ANDROID_KEYSTORE_PATH, ANDROID_KEYSTORE_PASSWORD,
#   ANDROID_KEY_ALIAS, ANDROID_KEY_PASSWORD

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
echo "For CI (GitHub Actions), add these as repository secrets:"
echo "  ANDROID_KEYSTORE_PATH    (base64-encode the .jks file)"
echo "  ANDROID_KEY_ALIAS        ($KEY_ALIAS)"
echo "  ANDROID_KEYSTORE_PASSWORD"
echo "  ANDROID_KEY_PASSWORD"
