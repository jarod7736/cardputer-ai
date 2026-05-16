from __future__ import annotations

import pytest

from cardputer_proxy.schemas import ChatCompletionRequest, Message


def test_chat_completion_request_minimal():
    req = ChatCompletionRequest(
        profile_id="claude-opus",
        messages=[Message(role="user", content="hello")],
    )
    assert req.stream is True
    assert req.profile_id == "claude-opus"
    assert len(req.messages) == 1


def test_chat_completion_request_rejects_empty_messages():
    with pytest.raises(ValueError):
        ChatCompletionRequest(profile_id="claude-opus", messages=[])


def test_message_rejects_unknown_role():
    with pytest.raises(ValueError):
        Message(role="emperor", content="hello")  # type: ignore[arg-type]
