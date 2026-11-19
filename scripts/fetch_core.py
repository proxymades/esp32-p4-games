#!/usr/bin/env python3
"""Fetch the pinned, unmodified Gwenesis core; verify Git blob hashes."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components" / "gwenesis"
MANIFEST = json.loads((COMPONENT / "upstream.json").read_text())
DEST = COMPONENT / "upstream"

def blob_hash(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

def fetch(entry):
    path = DEST / entry["path"]
    if path.is_file() and blob_hash(path.read_bytes()) == entry["git_blob_sha1"]:
        return
    url = ("https://raw.githubusercontent.com/giltal/ESP32_P4_Launcher/"
           + MANIFEST["revision"] + "/" + MANIFEST["prefix"] + entry["path"])
    with urllib.request.urlopen(url, timeout=45) as response:
        data = response.read()
    if len(data) != entry["size"] or blob_hash(data) != entry["git_blob_sha1"]:
        raise ValueError("Upstream integrity mismatch: " + entry["path"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(fetch, MANIFEST["files"]))
print("Verified", len(MANIFEST["files"]), "upstream files at", MANIFEST["revision"])
