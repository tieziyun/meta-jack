# CLAUDE.md

Yocto layer for a PYNQ-Z2 (Zynq XC7Z020) video/Qt system. Setup steps are in
[GETTING_STARTED.md](GETTING_STARTED.md); hardware background in
[hardware_design.md](hardware_design.md).

## Hard rules

- **Never run a real build** (`bitbake <target>`, `kas build`, `devtool build`,
  any `-c compile/fetch/...`) unless Jack explicitly orders it.
- **Never commit** (or push) unless Jack explicitly orders it.
- **Only edit files inside `meta-jack`.** Everything else under `sources/` is
  managed by kas; change layer pins in `kas.yml` instead.

## Layout

Project root is `~/GitHub/zynq-yocto-project` (this layer is
`sources/meta-jack`). Third-party layers sit under `sources/` and the build dir
is `build/`, both created by kas.

## Checking buildability (allowed without asking)

Use a parse-only run from the project root:

```bash
cd ~/GitHub/zynq-yocto-project
kas shell sources/meta-jack/kas.yml -c "bitbake -p"
```

Success looks like `Parsing of N .bb files complete (... 0 errors)`. The
warnings about `zynq-generic` being an include-only machine, the ZynqMP pmu-rom
and WSLv2 are expected noise.

## Host notes

- Host is Arch Linux on WSL2. kas is installed via pipx (`~/.local/bin/kas`).
- `HDF_PATH` (the Vivado XSA) and WiFi credentials (`WIFI_SSID`/`WIFI_PSK`)
  live in the gitignored `conf/jack-local.conf`, included from `conf/layer.conf`.
  bitbake re-parses it on change, so kas is only needed for layer checkout —
  never re-run kas just to pick up a settings change. Never print `WIFI_PSK`.
