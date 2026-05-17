"""Generic OAI-compatible adapter for vLLM, LM Studio, llama.cpp server, etc.

Same wire shape as OpenAI; the differences are operational (no implicit
endpoint default, optional auth).
"""

from __future__ import annotations

from cardputer_proxy.adapters.openai import OpenAIAdapter


class GenericOAIAdapter(OpenAIAdapter):
    pass


def _register() -> None:
    from cardputer_proxy.adapters import register
    register("openai-compatible", GenericOAIAdapter)


_register()
