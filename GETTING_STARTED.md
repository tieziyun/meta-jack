# Getting Started

Quick-start guide for setting up the FPGA-backed XDP SmartNIC project (PYNQ Z2 / Zynq XC7Z020) from scratch on a new machine.

All third-party Yocto layers are managed by [kas](https://kas.readthedocs.io/) and pinned in [`kas.yml`](kas.yml), which lives in this repo — you do **not** clone them by hand. The only repository you clone yourself is `meta-jack` (this one).

For the project background, architecture, and build details, see `readme.md` in the project root.

---

## 1. Prerequisites

- Arch Linux x86-64 host (native or WSL2), ~100 GB free disk, 16 GB+ RAM recommended
- Git configured with your name/email

Install the Yocto (Wrynose) host dependencies:

```bash
sudo pacman -S --needed base-devel git diffstat unzip texinfo chrpath socat \
  cpio python python-pip python-pexpect python-gitpython python-jinja \
  xz zstd lz4 file rpcsvc-proto inetutils net-tools wget which python-pipx
```

Note: Arch is not an officially supported Yocto host distro, but Wrynose builds fine on it. If bitbake complains about your locale, make sure `en_US.UTF-8` is enabled in `/etc/locale.gen` and run `sudo locale-gen`.

Install kas:

```bash
pipx install kas
```

---

## 2. Create the project layout and clone the repositories

```bash
mkdir -p ~/Projects/fpga-network-project/sources
cd ~/Projects/fpga-network-project

# The project layer — the only repo you clone manually
git clone https://github.com/tieziyun/meta-jack.git sources/meta-jack
```

Now let kas fetch every third-party layer at the exact pinned commit. Run from the **project root**:

```bash
kas checkout sources/meta-jack/kas.yml
```

This populates `sources/` with:

| Repo | Branch | Purpose |
|---|---|---|
| `openembedded-core` | wrynose | Core Yocto metadata |
| `bitbake` | 2.18 | Build engine |
| `meta-openembedded` | wrynose | meta-oe, meta-python |
| `meta-arm` | wrynose | ARM toolchain support |
| `meta-xilinx` | wrynose | Zynq BSP (v2026.1), `zynq-generic` machine, linux-xlnx |
| `meta-qt6` | 6.11 | Qt 6 for the UI |
| `meta-jack` | (main) | Project layer — kernel config, device tree, drivers, apps |

> **Do not edit anything under `sources/` except `sources/meta-jack/`.** Everything else is managed by kas; to move a layer forward, update its pin in `kas.yml` instead.

---

## 3. Point the build at your XSA

The hardware definition comes from a Vivado-exported `.xsa`. The path is machine-specific, so it lives in the gitignored `sources/meta-jack/conf/jack-local.conf` (included from `conf/layer.conf`; bitbake picks up edits on the next build, no kas re-run needed):

```bitbake
HDF_PATH = "/absolute/path/to/design_1_wrapper_fb.xsa"

# Optional: WiFi network baked into the image (RTL8188EUS dongle)
WIFI_SSID = "YourNetwork"
WIFI_PSK = "YourPassword"
```

The current working design is `design_1_wrapper_fb.xsa` (frmbuf display pipeline, Vivado 2025.2.1). Get a copy from Jack or export it from the Vivado project.

---

## 4. Build

From the project root:

```bash
source sources/openembedded-core/os-init-build-env
bitbake core-image-minimal
```

The first build fetches and compiles everything (several hours). Subsequent builds are incremental via `build/sstate-cache`.

Build output lands in `build/tmp/deploy/images/zynq-generic/` — boot binary (`boot.bin`), kernel, device tree, bitstream, and the rootfs/SD image.

---

## 5. Boot the board

1. Write the deployed WIC/SD image to a microSD card.
2. Set the PYNQ Z2 boot jumper to **SD**.
3. Connect UART (115200 8N1) and Ethernet, power on.
4. Verify networking: `ping`/SSH over the GEM interface (`eth0`).

Boot verification checklists are in `BOOT_TESTS.md` in the project root.

---

## Troubleshooting

- **kas fails to fetch a repo** — check network/proxy; kas clones over HTTPS, no credentials needed.
- **XSA/tooling mismatch errors** — the XSA's Vivado version must be compatible with the pinned meta-xilinx release (`rel-v2026.1`). Re-export the XSA from a matching Vivado if needed.
- **Branch mismatch errors between layers** — never mix pins by hand; `kas.yml` keeps oe-core, meta-openembedded, meta-arm and meta-xilinx on `wrynose` with bitbake `2.18`. Run `kas checkout --update sources/meta-jack/kas.yml` after changing pins.
