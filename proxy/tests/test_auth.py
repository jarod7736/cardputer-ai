from __future__ import annotations


def test_health_endpoint_no_auth_required(client):
    r = client.get("/healthz")
    assert r.status_code == 200
    assert r.json() == {"status": "ok"}


def test_chat_completions_rejects_missing_bearer(client):
    r = client.post("/v1/chat/completions", json={
        "profile_id": "claude-opus",
        "messages": [{"role": "user", "content": "hi"}],
    })
    assert r.status_code == 401


def test_chat_completions_rejects_wrong_bearer(client):
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer not-the-token"},
        json={
            "profile_id": "claude-opus",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 401


def test_chat_completions_accepts_correct_bearer_unknown_profile(client):
    r = client.post(
        "/v1/chat/completions",
        headers={"Authorization": "Bearer test-bearer"},
        json={
            "profile_id": "no-such-profile",
            "messages": [{"role": "user", "content": "hi"}],
        },
    )
    assert r.status_code == 404
