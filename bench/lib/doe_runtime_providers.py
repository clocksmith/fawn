"""Doe runtime CLI provider identities from config/doe-runtime-providers.json.

The registry is the one source for which native traces a Doe runtime CLI
wrote, which implementation (Zig or C) produced them, and how the CLI selected
its backend. Gates use it instead of literal executionBackend or binary-name
lists, so a C trace is recognized as Doe execution under its own name and is
never counted as a Zig trace.
"""

from __future__ import annotations

import functools
from dataclasses import dataclass
from pathlib import Path

from bench.lib.config_validation import load_validated_config

REPO_ROOT = Path(__file__).resolve().parents[2]
REGISTRY_PATH = Path("config/doe-runtime-providers.json")


@dataclass(frozen=True)
class DoeRuntimeProvider:
    provider_id: str
    runtime_implementation: str
    binary_name: str
    backend_selection: str
    execution_backends: tuple[tuple[str, str], ...]


@functools.lru_cache(maxsize=None)
def load_providers(root: Path = REPO_ROOT) -> tuple[DoeRuntimeProvider, ...]:
    """Load validated providers; duplicate binaries or backends are rejected."""
    payload = load_validated_config(root / REGISTRY_PATH)
    providers = tuple(
        DoeRuntimeProvider(
            provider_id=entry["providerId"],
            runtime_implementation=entry["runtimeImplementation"],
            binary_name=entry["binaryName"],
            backend_selection=entry["backendSelection"],
            execution_backends=tuple(
                (item["executionBackend"], item["api"]) for item in entry["executionBackends"]
            ),
        )
        for entry in payload["providers"]
    )
    for label, values in (
        ("providerId", [p.provider_id for p in providers]),
        ("binaryName", [p.binary_name for p in providers]),
        ("executionBackend", [b for p in providers for b, _ in p.execution_backends]),
    ):
        if len(set(values)) != len(values):
            raise ValueError(f"{REGISTRY_PATH}: duplicate {label}")
    return providers


def native_execution_backends() -> frozenset[str]:
    """Every executionBackend a Doe runtime CLI writes."""
    return frozenset(b for p in load_providers() for b, _ in p.execution_backends)


def native_execution_backends_for_api(api: str) -> frozenset[str]:
    return frozenset(b for p in load_providers() for b, a in p.execution_backends if a == api)


def provider_for_execution_backend(execution_backend: str) -> DoeRuntimeProvider | None:
    for provider in load_providers():
        if any(b == execution_backend for b, _ in provider.execution_backends):
            return provider
    return None


def runtime_binary_names() -> frozenset[str]:
    return frozenset(p.binary_name for p in load_providers())


def provider_for_binary(token: str) -> DoeRuntimeProvider | None:
    """The provider whose CLI a command token runs (by file name), else None."""
    name = Path(token).name
    for provider in load_providers():
        if provider.binary_name == name:
            return provider
    return None


def runtime_index(command: list[str]) -> int | None:
    """Index of the first Doe runtime CLI token in an argv list."""
    for index, token in enumerate(command):
        if provider_for_binary(token) is not None:
            return index
    return None


def template_uses_doe_runtime(template: str) -> bool:
    """Whether a command template runs a registered Doe runtime CLI."""
    return any(provider_for_binary(token) is not None for token in template.split())


def requires_selection_policy(execution_backend: str) -> bool:
    """True when the backend is chosen through backend-runtime-policy lanes."""
    provider = provider_for_execution_backend(execution_backend)
    return provider is None or provider.backend_selection == "backend-runtime-policy"
