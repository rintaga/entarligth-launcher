#!/usr/bin/env python3
"""Dijalankan oleh GitHub Actions. Membaca versi dari client.jar, lalu mengunduh
version JSON + library (tanpa LWJGL/natives) dari Mojang dan mengemasnya ke assets APK."""
import hashlib, json, os, sys, urllib.request, zipfile

OUT = "app/src/main/assets/game"
CLIENT = f"{OUT}/client.jar"
SERVER = f"{OUT}/server.jar"

for f in (CLIENT, SERVER):
    if not os.path.isfile(f):
        sys.exit(f"::error::{f} tidak ada. Taruh client.jar dan server.jar di {OUT}/ lalu commit.")

def get(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()

with zipfile.ZipFile(CLIENT) as z:
    version = json.loads(z.read("version.json"))["id"]
print("Versi dari client.jar:", version)

man = json.loads(get("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"))
ent = next((v for v in man["versions"] if v["id"] == version), None)
if not ent:
    sys.exit(f"::error::versi {version} tidak ada di manifest Mojang")
vj = json.loads(get(ent["url"]))

json.dump(vj, open(f"{OUT}/version.json", "w"))
ai = vj["assetIndex"]
open(f"{OUT}/assetindex.json", "wb").write(get(ai["url"]))
open(f"{OUT}/assetindex.id", "w").write(ai["id"])
open(f"{OUT}/version.id", "w").write(version)

def allowed(lib):
    rules = lib.get("rules")
    if not rules:
        return True
    ok = False
    for r in rules:
        o = r.get("os")
        match = True
        if o:
            if o.get("name") not in (None, "linux"):
                match = False
            if o.get("arch") == "x86":
                match = False
        if match:
            ok = (r["action"] == "allow")
    return ok

n = 0
with zipfile.ZipFile(f"{OUT}/libraries.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for lib in vj["libraries"]:
        if not allowed(lib):
            continue
        art = lib.get("downloads", {}).get("artifact")
        if not art:
            continue
        path = art["path"]
        if "lwjgl" in path or "natives" in path:
            continue  # LWJGL (fork Android) dan natives Linux disediakan terpisah
        data = get(art["url"])
        if hashlib.sha1(data).hexdigest() != art["sha1"]:
            sys.exit(f"::error::sha1 tidak cocok: {path}")
        z.writestr(path, data)
        n += 1
print(f"{n} library dikemas, java_version={vj.get('javaVersion')}")
