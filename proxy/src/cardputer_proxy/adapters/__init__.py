"""Adapter registry.

Each adapter module calls `register(provider, factory)` at import time.
Look up by provider name with `get(name)`.
"""

from __future__ import annotations

from typing import Callable

from cardputer_proxy.adapters.base import Adapter


_ADAPTERS: dict[str, Callable[[], Adapter]] = {}


def register(provider: str, factory: Callable[[], Adapter]) -> None:
    _ADAPTERS[provider] = factory


def get(provider: str) -> Adapter | None:
    factory = _ADAPTERS.get(provider)
    return factory() if factory else None


# Import side effect: each adapter file calls register(...) on import.
from cardputer_proxy.adapters import anthropic    # noqa: F401, E402
from cardputer_proxy.adapters import openai       # noqa: F401, E402
from cardputer_proxy.adapters import ollama       # noqa: F401, E402
from cardputer_proxy.adapters import generic_oai  # noqa: F401, E402
