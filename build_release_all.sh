#!/bin/bash
set -euo pipefail

TARGETS=(
  "x86_64-pc-windows-gnu:windows-x64:.exe"
  "aarch64-pc-windows-gnullvm:windows-arm64:.exe"
  "x86_64-unknown-linux-musl:linux-x64:"
  "aarch64-unknown-linux-musl:linux-arm64:"
  "aarch64-apple-darwin:macos-arm64:"
  "x86_64-apple-darwin:macos-x64:"
)

BINARIES=(
  "sc_client"
  "sc_server"
)

DIST_DIR="dist"
BASE_TARGET_DIR=$(
  cargo metadata --format-version 1 --no-deps \
    | sed -n 's/.*"target_directory":"\([^"]*\)".*/\1/p' \
    | head -n 1
)

if ! command -v cargo >/dev/null 2>&1; then
  echo "Error: cargo is not installed or not in PATH."
  exit 1
fi

if ! cargo zigbuild --help >/dev/null 2>&1; then
  echo "Error: cargo-zigbuild is not installed."
  echo "Install it with: cargo install cargo-zigbuild"
  exit 1
fi

echo "Detected global target directory: $BASE_TARGET_DIR"
echo "Binaries to build: ${BINARIES[*]}"
mkdir -p "$DIST_DIR"

echo "Adding Rust targets..."
for ITEM in "${TARGETS[@]}"; do
  IFS=':' read -r TARGET _ALIAS _EXT <<< "$ITEM"
  rustup target add "$TARGET"
done

for ITEM in "${TARGETS[@]}"; do
  IFS=':' read -r TARGET ALIAS EXT <<< "$ITEM"
  echo "-------------------------------------------"
  echo "Building target: $ALIAS ($TARGET)"

  for BIN in "${BINARIES[@]}"; do
    if cargo zigbuild --release --target "$TARGET" --bin "$BIN"; then
      SOURCE_PATH="${BASE_TARGET_DIR}/${TARGET}/release/${BIN}${EXT}"
      DEST_NAME="${BIN}-${ALIAS}${EXT}"
      cp "$SOURCE_PATH" "${DIST_DIR}/${DEST_NAME}"
      echo "Built: ${DIST_DIR}/${DEST_NAME}"
    else
      echo "Build failed: target=$TARGET bin=$BIN"
    fi
  done
done

echo "-------------------------------------------"
echo "Done. Artifacts are available in: ${DIST_DIR}/"
