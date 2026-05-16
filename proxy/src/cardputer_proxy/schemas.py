"""Pydantic models for the device-facing wire protocol (OAI-compatible)."""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, field_validator


Role = Literal["system", "user", "assistant"]


class Message(BaseModel):
    role: Role
    content: str


class ChatCompletionRequest(BaseModel):
    profile_id: str = Field(..., min_length=1)
    messages: list[Message] = Field(..., min_length=1)
    stream: bool = True
    max_tokens: int | None = Field(default=None, ge=1, le=200_000)
    temperature: float | None = Field(default=None, ge=0.0, le=2.0)

    @field_validator("messages")
    @classmethod
    def _at_least_one_user_or_assistant(cls, v: list[Message]) -> list[Message]:
        if not any(m.role in ("user", "assistant") for m in v):
            raise ValueError("messages must include at least one user or assistant turn")
        return v


class Profile(BaseModel):
    id: str
    label: str
    provider: Literal["anthropic"]
    endpoint: str
    model: str
    max_tokens: int = 4096
    temperature: float = 1.0
