
l
#!/usr/bin/env bash
# Mengunduh JRE Android 32-bit (ARM) lalu mengemasnya jadi app/src/main/assets/jre-arm.zip
# JRE_ARM_URL boleh berisi beberapa URL dipisah spasi (mis. paket "universal" + "bin-arm").
# Minecraft 26.x butuh Java 25.
set -euo pipefail
REPO="$PWD"
OUT="$REPO/app/src/main/assets"
mkdir -p "$OUT"
urls="$(printf '%s' "${JRE_ARM_URL:-}" | tr -d '\r')"
if [ -z "$urls" ]; then
  echo "::warning::JRE_ARM_URL kosong, runtime dilewati (APK tidak bisa menjalankan Java)"
  exit 0
fi
tmp="$(mktemp -d)"; x="$tmp/x"; mkdir "$x"
i=0
for u in $urls; do
  i=$((i+1))
  curl -fL --retry 3 "$u" -o "$tmp/pkg$i"
  case "$u" in
    *.zip) unzip -q "$tmp/pkg$i" -d "$x" ;;
    *)     tar -xf "$tmp/pkg$i" -C "$x" ;;
  esac
done
root="$x"
if [ "$(ls -A "$root" | wc -l)" = 1 ] && [ -d "$root/$(ls -A "$root")" ]; then
  root="$root/$(ls -A "$root")"
fi
if [ ! -f "$root/lib/libjli.so" ]; then
  echo "::error::lib/libjli.so tidak ditemukan di runtime"; ls -R "$root" | head -50; exit 1
fi
(cd "$root" && zip -qr "$OUT/jre-arm.zip" .)
echo "OK: jre-arm.zip ($(du -h "$OUT/jre-arm.zip" | cut -f1))"
