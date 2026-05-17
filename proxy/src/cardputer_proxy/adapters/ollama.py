"""Ollama adapter — uses Ollama's OpenAI-compatible endpoint."""

from __future__ import annotations

from cardputer_proxy.adapters.openai import OpenAIAdapter


class OllamaAdapter(OpenAIAdapter):
    # Same path, same body shape, same chunk shape as OpenAI. Ollama
    # ignores Authorization headers; we still send one if secret is set
    # (harmless) and skip it for the typical auth.kind="none" profile.
    pass


def _register() -> None:
    from cardputer_proxy.adapters import register
    register("ollama", OllamaAdapter)


_register()
