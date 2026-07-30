# super-tamagotchi

A physical virtual pet built on an ESP32-S3, with a 2.8" colour TFT, rotary and directional input,
and a speaker.

Hardware inventory, wiring constraints, and project conventions live in [AGENTS.md](AGENTS.md).

## Status

Early scaffolding — hardware inventory only, no firmware yet.

## Configuration

WiFi credentials and any other secrets belong in a local, gitignored config file — never in
committed firmware sources. See the secrets note in [AGENTS.md](AGENTS.md).

```bash
cp .env.example .env
```
