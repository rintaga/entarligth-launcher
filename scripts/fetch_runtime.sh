#!/usr/bin/env bash
# Mengunduh JRE Android lalu mengemasnya jadi app/src/main/assets/jre-<abi>.zip
# JRE_ARM64_URL / JRE_ARM_URL boleh berisi beberapa URL dipisah spasi
# (mis. paket "universal" + paket "bin-arm64" dari build PojavLauncher).
set -euo pipefail
REPO="$PWD"
OUT="$REPO/app/src/main/assets"
mkdir -p "$OUT"

pack() {
  local abi="$1" urls="${2:-}"
  if [ -z "$urls" ]; then
    echo "::warning::URL runtime $abi kosong, dilewati"
    return 0
  fi
  local tmp x; tmp="$(mktemp -d)"; x="$tmp/x"; mkdir "$x"
  local i=0 u
  for u in $urls; do
    i=$((i+1))
    curl -fL --retry 3 "$u" -o "$tmp/pkg$i"
    case "$u" in
      *.zip) unzip -q "$tmp/pkg$i" -d "$x" ;;
      *)     tar -xf "$tmp/pkg$i" -C "$x" ;;
    esac
  done
  local root="$x"
  if [ "$(ls -A "$root" | wc -l)" = 1 ] && [ -d "$root/$(ls -A "$root")" ]; then
    root="$root/$(ls -A "$root")"
  fi
  if [ ! -f "$root/lib/libjli.so" ]; then
    echo "::error::lib/libjli.so tidak ditemukan di runtime $abi"; ls -R "$root" | head -50; exit 1
  fi
  (cd "$root" && zip -qr "$OUT/jre-$abi.zip" .)
  echo "OK: jre-$abi.zip ($(du -h "$OUT/jre-$abi.zip" | cut -f1))"
}

pack arm64 "${JRE_ARM64_URL:-}"
pack arm   "${JRE_ARM_URL:-}"
