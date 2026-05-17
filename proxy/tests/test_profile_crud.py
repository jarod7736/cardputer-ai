from __future__ import annotations


def auth():
    return {"Authorization": "Bearer test-bearer"}


def test_list_profiles_returns_seeded(client):
    r = client.get("/v1/profiles", headers=auth())
    assert r.status_code == 200
    body = r.json()
    assert any(p["id"] == "claude-opus" for p in body["profiles"])


def test_get_profile_404_for_unknown(client):
    r = client.get("/v1/profiles/no-such", headers=auth())
    assert r.status_code == 404


def test_create_profile(client):
    body = {
        "id": "openai-mini",
        "label": "OpenAI gpt-mini",
        "provider": "openai",
        "endpoint": "https://api.openai.com",
        "model": "gpt-4o-mini",
        "max_tokens": 1024,
        "temperature": 0.7,
    }
    r = client.post("/v1/profiles", json=body, headers=auth())
    assert r.status_code == 201
    assert r.json()["id"] == "openai-mini"

    r2 = client.get("/v1/profiles", headers=auth())
    assert any(p["id"] == "openai-mini" for p in r2.json()["profiles"])


def test_create_profile_rejects_duplicate(client):
    body = {
        "id": "claude-opus",
        "label": "dup",
        "provider": "anthropic",
        "endpoint": "https://api.anthropic.com",
        "model": "claude-opus-4-7",
    }
    r = client.post("/v1/profiles", json=body, headers=auth())
    assert r.status_code == 409


def test_patch_profile(client):
    r = client.patch(
        "/v1/profiles/claude-opus",
        json={"label": "Renamed"},
        headers=auth(),
    )
    assert r.status_code == 200
    assert r.json()["label"] == "Renamed"


def test_delete_profile(client):
    r = client.delete("/v1/profiles/claude-opus", headers=auth())
    assert r.status_code == 204
    r2 = client.get("/v1/profiles/claude-opus", headers=auth())
    assert r2.status_code == 404


def test_profile_routes_require_auth(client):
    assert client.get("/v1/profiles").status_code == 401
    assert client.post("/v1/profiles", json={}).status_code == 401
    assert client.patch("/v1/profiles/x", json={}).status_code == 401
    assert client.delete("/v1/profiles/x").status_code == 401
