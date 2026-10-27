#!/usr/bin/env python3
"""Export a BANG! TorchScript checkpoint for browser-native inference."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import torch


FORMAT = "bang_static_policy_v1"
RULESET = "base_four_character"
# v1 checkpoints predate recorded layouts and always use the base one; v2
# checkpoints record theirs under "layout", and the C++ loader widens any
# layout smaller than the current one when it reads the export.
ABIS = {
    ("bang_training_observation_v1", "bang_action_feature_v1"),
    ("bang_training_observation_v2", "bang_action_feature_v2"),
}
V1_SIZES = {"observationSize": 1229, "actionFeatureSize": 91}
# Residual-block counts per model type, mirroring PolicyArchitecture in
# include/bang/policy_architecture.hpp. Layer widths are read from the tensors
# themselves, so only the block count has to be known here.
RESIDUAL_BLOCKS = {
    "bang_structured_v2": 2,
    "bang_structured_v3": 4,
}


def actor_parameters(model_type: str) -> tuple[str, ...]:
    if model_type not in RESIDUAL_BLOCKS:
        raise ValueError(f"unsupported model type: {model_type}")
    modules = ["public_encoder.0", "identity_encoder.0", "state_fusion.0"]
    modules += [f"residuals.{block}" for block in range(RESIDUAL_BLOCKS[model_type])]
    modules += ["state_policy.0", "action_encoder.0", "actor.0"]
    return tuple(f"{name}.{suffix}" for name in modules for suffix in ("weight", "bias"))


def load_metadata(checkpoint: Path) -> dict[str, Any]:
    path = Path(f"{checkpoint}.metadata.json")
    if not path.is_file():
        raise FileNotFoundError(f"missing checkpoint metadata: {path}")
    metadata = json.loads(path.read_text())
    mismatches = []
    if metadata.get("ruleset") != RULESET:
        mismatches.append("ruleset")
    abi = (metadata.get("observationAbi"), metadata.get("actionFeatureAbi"))
    if abi not in ABIS:
        mismatches.append("observationAbi/actionFeatureAbi")
    elif abi[0].endswith("_v1"):
        mismatches += [key for key, size in V1_SIZES.items() if metadata.get(key) != size]
    elif not isinstance(metadata.get("layout"), dict):
        mismatches.append("layout")
    if metadata.get("modelType") not in RESIDUAL_BLOCKS:
        mismatches.append("modelType")
    if mismatches:
        raise ValueError(
            "checkpoint is incompatible with the static policy ABI: "
            + ", ".join(mismatches)
        )
    return metadata


def tensor_payload(tensor: torch.Tensor) -> dict[str, Any]:
    cpu = tensor.detach().cpu().to(torch.float32).contiguous()
    return {
        "shape": list(cpu.shape),
        "values": cpu.reshape(-1).tolist(),
    }


def export_checkpoint(checkpoint: Path, output: Path) -> None:
    if not checkpoint.is_file():
        raise FileNotFoundError(f"checkpoint does not exist: {checkpoint}")
    metadata = load_metadata(checkpoint)
    module = torch.jit.load(str(checkpoint), map_location="cpu")
    module.eval()
    parameters = dict(module.named_parameters())
    actor = actor_parameters(metadata["modelType"])
    missing = [name for name in actor if name not in parameters]
    if missing:
        raise ValueError(
            "checkpoint is missing actor parameters: " + ", ".join(missing)
        )
    payload = {
        "format": FORMAT,
        "sourceCheckpoint": checkpoint.name,
        "metadata": metadata,
        "parameters": [
            {"name": name, "tensor": tensor_payload(parameters[name])}
            for name in actor
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, separators=(",", ":")))
    temporary.replace(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    export_checkpoint(args.checkpoint, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
