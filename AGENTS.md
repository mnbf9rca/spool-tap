# spool-tap

Firmware for an M5Stack CoreS3 SE with a Grove U216 (ST25R3916) NFC reader.
Tap a filament spool's tag to register it as the loaded spool in FilaBridge, with spool details from Spoolman.

## Working rules

Multi-agent work uses the three-seat arrangement in `docs/agent-teams.md`: chief of staff, builder (Codex, `implementer`) and reviewer (Claude, `k8s-reviewer`).
Read it before dispatching any agent.
A design review includes a seat whose only brief is deletion: name machinery that exists to feel rigorous, and what deleting it would lose.
Specs, plans and handover files live under `.superpowers/sdd/<plan>/`, which is gitignored; handovers are absolute file paths, never pasted output.
Markdown is not hard-wrapped: one sentence per line.
Anything that asks the operator to act is one instruction per sentence, at most 20 words, imperative.
Never commit a secret value. The FilaBridge and Spoolman hostnames are LAN-only identifiers and may be committed.
Nothing is published in the operator's name (GitHub push, upstream issue or PR) without explicit approval.
