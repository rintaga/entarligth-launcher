#!/usr/bin/env bash
set -euo pipefail
REPO="$PWD"; OUT="$REPO/app/src/main/assets"; mkdir -p "$OUT"
urls="$(printf '%s' "${JRE_ARM_URL:-}" | tr '\r\n\t' '   ')"
if [ -z "${urls// /}" ]; then echo "::warning::JRE_ARM_URL kosong, dilewati"; exit 0; fi
tmp="$(mktemp -d)"; x="$tmp/x"; mkdir "$x"; i=0
for u in $urls; do
  i=$((i+1)); echo "Unduh: $u"
  curl -fL --retry 3 "$u" -o "$tmp/pkg$i"
  kind="$(file -b "$tmp/pkg$i")"; echo "Jenis file: $kind"
  case "$kind" in
    *Zip*) unzip -q "$tmp/pkg$i" -d "$x" ;;
    *XZ*|*gzip*|*tar*|*bzip2*) tar -xf "$tmp/pkg$i" -C "$x" ;;
    *) echo "::error::Bukan arsip. Isi awal file:"; head -c 300 "$tmp/pkg$i"; echo; exit 1 ;;
  esac
done
root="$x"
if [ "$(ls -A "$root" | wc -l)" = 1 ] && [ -d "$root/$(ls -A "$root")" ]; then root="$root/$(ls -A "$root")"; fi
[ -f "$root/lib/libjli.so" ] || { echo "::error::lib/libjli.so tidak ada"; ls -R "$root" | head -50; exit 1; }
(cd "$root" && zip -qr "$OUT/jre-arm.zip" .)
echo "OK: $(du -h "$OUT/jre-arm.zip" | cut -f1)"
