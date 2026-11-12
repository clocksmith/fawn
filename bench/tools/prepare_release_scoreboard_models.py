"""Prepare a Doppler model directory for the release-scoreboard rows.

The scoreboard loads Doppler's tracked manifest and tokenizer for a model
together with shard bytes that already exist on the host (for example a
Reploid or benchmark-stock copy). This tool links (or copies) those shards
beside the tracked metadata, hashes every shard once against the tracked
manifest, and writes ``custody.json``. The executors then check only the
manifest digest, the custody receipt and shard sizes on each run, which keeps
multi-gigabyte hashing out of cold-start measurements.

Nothing is downloaded. A shard whose bytes do not match the tracked manifest
fails preparation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT_ROOT = REPO_ROOT / "bench" / "out" / "release-scoreboard" / "models"
DEFAULT_DOPPLER_ROOT = REPO_ROOT.parent / "doppler"
METADATA_SUFFIXES = (".json", ".model", ".txt")
CHUNK_BYTES = 1 << 24


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--model-id", required=True, help="Doppler model id (directory name under models/local).")
    parser.add_argument("--shard-dir", required=True, type=Path,
                        help="Existing directory holding this model's shard_*.bin files.")
    parser.add_argument("--doppler-root", type=Path, default=DEFAULT_DOPPLER_ROOT,
                        help="Doppler checkout whose tracked manifest and tokenizer are used.")
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT_ROOT,
                        help="Prepared model root (the executors read FAWN_SCOREBOARD_MODEL_ROOT or this default).")
    parser.add_argument("--copy", action="store_true",
                        help="Copy shards instead of symlinking them.")
    return parser.parse_args(argv)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def prepare(model_id: str, shard_dir: Path, doppler_root: Path, out_root: Path, copy: bool) -> dict:
    """Assemble and verify one model directory; return its custody record."""
    metadata_dir = doppler_root / "models" / "local" / model_id
    manifest_path = metadata_dir / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"no tracked Doppler manifest at {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    shards = manifest.get("shards")
    if not isinstance(shards, list) or not shards:
        raise ValueError(f"{manifest_path} declares no shards")
    target = out_root / model_id
    target.mkdir(parents=True, exist_ok=True)
    for source in sorted(metadata_dir.iterdir()):
        if source.suffix in METADATA_SUFFIXES and source.resolve().is_file():
            shutil.copyfile(source.resolve(), target / source.name)
    records = []
    for shard in shards:
        name = str(shard["filename"])
        source = (shard_dir / name).resolve()
        if not source.is_file():
            raise FileNotFoundError(f"shard {name} is missing from {shard_dir}")
        actual = sha256_file(source)
        expected = str(shard.get("sha256") or shard.get("hash"))
        if actual != expected or source.stat().st_size != int(shard["size"]):
            raise ValueError(f"shard {name} does not match the tracked manifest ({actual} != {expected})")
        destination = target / name
        if destination.is_symlink() or destination.exists():
            destination.unlink()
        if copy:
            shutil.copyfile(source, destination)
        else:
            destination.symlink_to(source)
        records.append({"filename": name, "sha256": actual, "size": int(shard["size"])})
    custody = {
        "schemaVersion": 1,
        "modelId": model_id,
        "manifestSha256": sha256_file(target / "manifest.json"),
        "allShardsVerified": True,
        "shardMaterialization": "copy" if copy else "symlink",
        "shards": records,
    }
    (target / "custody.json").write_text(json.dumps(custody, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return custody


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        custody = prepare(args.model_id, args.shard_dir, args.doppler_root, args.out_root, args.copy)
    except (FileNotFoundError, ValueError) as exc:
        print(f"prepare failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({"modelId": custody["modelId"], "manifestSha256": custody["manifestSha256"],
                      "shards": len(custody["shards"])}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
