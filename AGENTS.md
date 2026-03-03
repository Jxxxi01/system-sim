# AGENTS.md (system-sim)

## Goal
Build a pure CLI functional simulator demo.

## Definition of Done
- `make demo` runs end-to-end and produces `out/audit.jsonl`
- `make test` passes

## Commands
- Prefer Python.
- Use `python -m venv .venv` and `pip install -r requirements.txt`
- Testing: `pytest -q`

## Project rules
- Keep code minimal and readable.
- Deterministic randomness via seed in scenarios.
- All actions must emit structured audit events.
