# spool-tap

Firmware for an M5Stack CoreS3 SE with a Grove U216 (ST25R3916) NFC reader.
Tap a filament spool's tag to register it as the loaded spool in FilaBridge, with spool details from Spoolman.

## Working rules

Multi-agent work uses the three-seat arrangement defined in `/Users/rob/git/kubernetes_config/docs/operations/agent-teams.md`.
Read it before dispatching any agent.
Specs and plans live under `.superpowers/sdd/<plan>/`, which is gitignored.
Markdown is not hard-wrapped: one sentence per line.
Never commit a secret value; the FilaBridge and Spoolman hosts are LAN-only identifiers and may be committed.
