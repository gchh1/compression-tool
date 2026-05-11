from __future__ import annotations


def pytest_sessionstart(session) -> None:
    # WCX read/write is core_engine-only; no env-based compat toggles.
    pass
