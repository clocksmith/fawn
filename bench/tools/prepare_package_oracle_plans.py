"""Prepare new full-span package plans with independent CPU output references.

The original catalogs, plans, shaders and retained evidence remain untouched.
Vector references admit exactly the separately rounded and contracted multiply
add results for these finite float32 inputs. Image references use exact u32 XOR.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from bench.lib import synthetic_assets


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = {
    "package_vector_scale_add_262k": (
        "package_vector_scale_add.wgsl",
        "8f2b3e2bb6955c1f7507936773589195c80e6a119cbd50d975f652d4f834e8c1"),
    "package_image_rgba_invert_1024": (
        "package_image_rgba_invert.wgsl",
        "687a30bc57f40bc94d60e970fc59a140d550890176a91816e1ca1f17a140337a"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def vector_reference(data: bytes) -> dict[str, object]:
    """Declare both legal roundings without consulting either GPU provider.

    Binary64 exactly represents x*1.5+0.25 for the frozen bounded float32
    domain, so one final f32 conversion gives the fused result. An intervening
    f32 conversion gives the split result. No generic ULP relaxation is used.
    """
    values = []
    for (value,) in struct.iter_unpack("<f", data):
        if not 2.0**-13 <= abs(value) <= 0.125:
            raise ValueError("vector reference requires the frozen finite input domain")
        split = f32(f32(value * 1.5) + 0.25)
        fused = f32(value * 1.5 + 0.25)
        values.append([split] if split == fused else [split, fused])
    return {"kind": "f32BufferOneOfV1", "values": values}


def image_reference(data: bytes) -> bytes:
    output = bytearray(len(data))
    for index, (value,) in enumerate(struct.iter_unpack("<I", data)):
        struct.pack_into("<I", output, index * 4, value ^ 0x00FFFFFF)
    return bytes(output)


def prepare(out: Path) -> dict[str, object]:
    out.mkdir(parents=True, exist_ok=False)
    receipts = []
    for workload_id, (shader_name, shader_hash) in FIXTURES.items():
        source = ROOT / "bench/plans/package-developer" / (workload_id + ".plan.json")
        shader = ROOT / "bench/kernels" / shader_name
        if sha256(shader.read_bytes()) != shader_hash:
            raise ValueError(f"shader changed outside this CPU reference contract: {shader}")
        plan = json.loads(source.read_text(encoding="utf-8"))
        load, parameters, dispatch = plan["commands"]
        if (load["kind"] != "buffer_load" or parameters["kind"] != "buffer_write"
                or dispatch["kind"] != "kernel_dispatch"
                or dispatch["kernel"] != shader_name
                or parameters["data"] != [load["byteLength"] // 4, 0, 0, 0]):
            raise ValueError("fixture command structure changed")
        data = synthetic_assets.generate_asset_bytes(
            generator=load["generator"], seed=load["seed"],
            scale=load["scale"], byte_length=load["byteLength"])
        if shader_name == "package_vector_scale_add.wgsl":
            reference = vector_reference(data)
        else:
            reference = {"kind": "sha256Equals", "sha256": sha256(image_reference(data))}
        dispatch["captureSize"] = len(data)
        dispatch["captureValidate"] = reference
        plan["description"] += " Prospective v1 independent CPU oracle over the full output span."
        destination = out / (workload_id + ".cpu-oracle-v1.plan.json")
        encoded = (json.dumps(plan, separators=(",", ":")) + "\n").encode()
        destination.write_bytes(encoded)
        receipts.append({"workloadId": workload_id, "plan": str(destination.resolve()),
                         "planSha256": sha256(encoded), "sourcePlanSha256": sha256(source.read_bytes()),
                         "shaderPath": str(shader.relative_to(ROOT)), "shaderSha256": shader_hash,
                         "inputSha256": sha256(data), "fullOutputBytes": len(data),
                         "referenceKind": reference["kind"], "referenceClass": "independent_cpu_v1"})
    manifest = {"schemaVersion": 1, "scope": "new full-span output contracts; no historical qualification",
                "generatorSha256": sha256(Path(__file__).read_bytes()), "plans": receipts}
    (out / "oracle-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path, help="Fresh prospective plan directory.")
    print(json.dumps(prepare(parser.parse_args().out), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
