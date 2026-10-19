# meta-jack

A Yocto layer that builds a complete, bootable Linux system for the **PYNQ-Z2** board (AMD/Xilinx Zynq XC7Z020). It drives an **HDMI display from the FPGA** and runs a **Qt status panel** on it.

One command builds everything the SD card needs: the first-stage bootloader, U-Boot, the FPGA bitstream, the device tree, the Linux kernel with custom display drivers, and a root filesystem. You don't need Vivado, Vitis or PetaLinux to build it. You only need the `.xsa` hardware file exported from Vivado.

```
 Vivado .xsa ──► meta-jack + Yocto (kas) ──► SD card image ──► PYNQ-Z2 boots ──► Qt panel on HDMI
```

> **New to Yocto?** Start with [What is all this?](#what-is-all-this-a-two-minute-primer) below, then follow the [step-by-step guide](GETTING_STARTED.md).
> **Already know Yocto?** Skip to the [Quick start](#quick-start-tldr) and the [Layer contents](#whats-in-the-layer) table.

---

## Contents

- [What you get on the board](#what-you-get-on-the-board)
- [What you need](#what-you-need)
- [What is all this? (a two-minute primer)](#what-is-all-this-a-two-minute-primer)
- [Quick start (TL;DR)](#quick-start-tldr)
- [Flash and boot](#flash-and-boot)
- [What's in the layer](#whats-in-the-layer)
- [Common tasks](#common-tasks)
- [How the boot works](#how-the-boot-works)
- [Troubleshooting](#troubleshooting)
- [Further reading](#further-reading)

---

## What you get on the board

| Feature | Details |
|---|---|
| **HDMI display** | FPGA video pipeline (Video Frame Buffer Read → VTC → `rgb2dvi`) driven by the Xilinx DRM driver plus two out-of-tree drivers: `clk-dglnt-dynclk` (pixel clock) and `digilent-hdmi` (HDMI encoder). Shows up as `/dev/dri/card0` and `/dev/fb0`. |
| **Qt 6 UI** | `jack-panel`: a fullscreen Qt Widgets app showing FPGA state, CPU load, memory and display IRQ rate. It starts automatically on boot. The board has no GPU, so Qt draws in software to the framebuffer (`linuxfb`). |
| **FPGA bitstream** | Taken from your `.xsa` and loaded by U-Boot before Linux starts. A copy is also installed in `/lib/firmware` for `fpga-manager`. |
| **Networking** | Gigabit Ethernet (`eth0`). WiFi (`wlan0`) works through an optional **RTL8188EUS** USB dongle, with DHCP from `systemd-networkd`. |
| **Init system** | `systemd` |
| **Debug tools** | `modetest` (DRM), `devmem2` (raw register access), `htop`, `usbutils`, plus `debugfs` enabled in the kernel |
| **Login** | `root` with an **empty password** on the serial console. This is a development setting; see [Security note](#security-note). |

---

## What you need

**Hardware**
- PYNQ-Z2 board, a micro-USB cable (power + serial console), a microSD card (≥ 2 GB)
- An HDMI monitor connected to the **HDMI OUT** port
- Optional: an Ethernet cable, and an RTL8188EUS USB WiFi dongle

**Build machine**
- Linux x86-64, native or **WSL2** (developed on Arch Linux; any distro [supported by Yocto](https://docs.yoctoproject.org/ref-manual/system-requirements.html) should work)
- **~100 GB free disk** and **16 GB+ RAM**. More CPU cores make the build much faster.
- An internet connection for the first build, which downloads several GB of source code

**Hardware design**
- A Vivado-exported `.xsa` file for the PYNQ-Z2 video design (currently `design_1_wrapper_fb.xsa`, Vivado 2025.2.1). It has to match the device tree in this layer. See [hardware_design.md](hardware_design.md).

---

## What is all this? (a two-minute primer)

If you have never used Yocto, these are the few terms you need:

| Term | Plain-English meaning |
|---|---|
| **Yocto / OpenEmbedded** | A toolkit that builds a whole custom Linux distribution from source: compiler, kernel, libraries, apps and the final disk image. |
| **BitBake** | The build engine Yocto uses. You ask it for a *target* (e.g. `core-image-minimal`) and it works out and runs every step needed. |
| **Recipe (`.bb`)** | Instructions for building one piece of software: where to get it, how to compile it, what to install. |
| **Append (`.bbappend`)** | A patch-like file that changes someone else's recipe without editing it. |
| **Layer (`meta-*`)** | A folder of related recipes and config. This repo, **meta-jack**, is the project's layer. Other layers provide the core system, Xilinx support, Qt and so on. |
| **kas** | A small tool that downloads all the layers at exact, pinned versions and sets up the build, using [`kas.yml`](kas.yml). It's the reason you only clone one repo by hand. |
| **XSA** | The hardware description Vivado exports. It contains the FPGA bitstream and the processor setup (`ps7_init`). |
| **FSBL** | First Stage Boot Loader. The first program the Zynq runs from the SD card; it sets up clocks and DDR memory. |
| **WIC image** | A ready-to-flash SD card image with the boot and root partitions already in place. |

Want to know how BitBake gets from `kas.yml` to an SD card image? Read [how-bitbake-works.md](how-bitbake-works.md), which walks through it using this project.

---

## Quick start (TL;DR)

These steps assume an Arch Linux host. For other distros, install the [Yocto host packages](https://docs.yoctoproject.org/ref-manual/system-requirements.html#required-packages-for-the-build-host) for your distro instead. Full step-by-step details with explanations are in **[GETTING_STARTED.md](GETTING_STARTED.md)**.

```bash
# 1. Host tools (Arch Linux; see GETTING_STARTED.md for the package list)
pipx install kas

# 2. Project layout: you clone only this repo, kas fetches the rest
mkdir -p ~/zynq-yocto-project/sources && cd ~/zynq-yocto-project
git clone https://github.com/tieziyun/meta-jack.git sources/meta-jack
kas checkout sources/meta-jack/kas.yml

# 3. Tell the build where your hardware design is (this file is gitignored)
cat > sources/meta-jack/conf/jack-local.conf <<'EOF'
HDF_PATH = "/absolute/path/to/design_1_wrapper_fb.xsa"
EOF

# 4. Check that everything parses (fast, takes about a minute)
kas shell sources/meta-jack/kas.yml -c "bitbake -p"

# 5. Build (the first run takes several hours)
kas build sources/meta-jack/kas.yml
```

After this you will have:

```
zynq-yocto-project/          ← "project root": run all commands from here
├── sources/
│   ├── meta-jack/           ← this repo, the only one you edit
│   ├── openembedded-core/   ┐
│   ├── bitbake/             │
│   ├── meta-openembedded/   │ fetched and pinned by kas,
│   ├── meta-arm/            │ do not edit
│   ├── meta-xilinx/         │
│   └── meta-qt6/            ┘
└── build/                   ← build output, caches, downloads
    └── tmp/deploy/images/zynq-generic/   ← the files you flash
```

> **Prefer plain BitBake?** Once `kas checkout` has run, you can also build with
> `source sources/openembedded-core/oe-init-build-env && bitbake core-image-minimal`.
> Both ways use the same `build/` directory.

> **VS Code tip:** the project root is not a git repo itself, so the Source Control panel won't show `meta-jack` until you add `.vscode/settings.json` in the project root containing `{ "git.scanRepositories": ["sources/meta-jack"] }`, or open `sources/meta-jack` as the workspace folder.

---

## Flash and boot

The image to flash is:

```
build/tmp/deploy/images/zynq-generic/core-image-minimal-zynq-generic.rootfs.wic
```

**On native Linux** (replace `/dev/sdX` with your SD card, and **double-check it**: writing to the wrong disk erases it):

```bash
cd build/tmp/deploy/images/zynq-generic
sudo bmaptool copy core-image-minimal-zynq-generic.rootfs.wic /dev/sdX
# or, without bmaptool (slower):
sudo dd if=core-image-minimal-zynq-generic.rootfs.wic of=/dev/sdX bs=4M conv=fsync status=progress
```

**On WSL2**, WSL can't see SD card readers by default. Copy the `.wic` file to Windows (it's under `\\wsl$\<distro>\...` in Explorer) and write it with a tool such as [balenaEtcher](https://etcher.balena.io/) or [Rufus](https://rufus.ie/).

**Booting the board:**

1. Set the PYNQ-Z2 boot-mode jumper to **SD**.
2. Insert the SD card, connect the HDMI monitor, and plug the micro-USB cable into your PC.
3. Open the serial console at **115200 8N1**. On Linux use something like `picocom -b 115200 /dev/ttyUSB1`; on Windows use PuTTY with the matching COM port. The board shows up as two serial ports: one is JTAG, the other is the UART.
4. Power on. You should see U-Boot and then Linux boot messages. Log in as `root` with no password.
5. After a few seconds the **jack-panel** UI appears on the HDMI monitor.

Quick checks once you're logged in:

```bash
cat /sys/class/fpga_manager/fpga0/state   # "operating" means the bitstream is loaded
modetest -M xlnx                          # list DRM connectors/modes of the display pipeline
systemctl status jack-panel               # is the Qt UI running?
ip addr                                   # eth0 / wlan0 addresses
```

---

## What's in the layer

| Path | What it does |
|---|---|
| [`kas.yml`](kas.yml) | **Start here.** Lists every layer and its pinned commit (Yocto **wrynose**, BitBake 2.18, meta-xilinx 2026.1, Qt 6.11), the machine (`zynq-generic`), distro (`nodistro`) and image target. |
| [`conf/layer.conf`](conf/layer.conf) | Layer registration, plus the image settings: systemd, WIC SD image, which packages go into the image, and the generic (no-Vitis) embedded-software flow (`XILINX_WITH_ESW = "generic"`). |
| `conf/jack-local.conf` | **Your machine's settings, gitignored**: `HDF_PATH`, optional `WIFI_SSID`/`WIFI_PSK`. |
| [`recipes-bsp/embeddedsw/`](recipes-bsp/embeddedsw/) | Builds the **FSBL** with the Yocto cross-compiler, using `ps7_init` and the UART/DDR parameters read directly from the XSA. |
| [`recipes-bsp/bitstream/`](recipes-bsp/bitstream/) | Pulls the `.bit` out of the XSA and converts it with `bootgen` into the byte-swapped `.bin` that U-Boot and `fpga-manager` load. |
| [`recipes-bsp/device-tree/`](recipes-bsp/device-tree/) | The hand-written board device tree ([`pynq-z2.dts`](recipes-bsp/device-tree/files/pynq-z2.dts)) and the programmable-logic devices ([`pynq-z2-pl.dtsi`](recipes-bsp/device-tree/files/pynq-z2-pl.dtsi)). |
| [`recipes-bsp/bootbin/`](recipes-bsp/bootbin/), [`u-boot/`](recipes-bsp/u-boot/) | Small fixes so `BOOT.bin` picks up our DTB, and a dependency loop in meta-xilinx is broken. |
| [`recipes-kernel/linux/`](recipes-kernel/linux/) | Kernel config fragments ([`display.cfg`](recipes-kernel/linux/files/display.cfg), [`wifi.cfg`](recipes-kernel/linux/files/wifi.cfg), [`debug.cfg`](recipes-kernel/linux/files/debug.cfg)) and a patch that lets the Xilinx DRM driver bind the Digilent HDMI encoder. |
| [`recipes-kernel/clk-dglnt-dynclk/`](recipes-kernel/clk-dglnt-dynclk/) | Out-of-tree kernel module for the Digilent dynamic **pixel clock** generator. |
| [`recipes-kernel/digilent-hdmi/`](recipes-kernel/digilent-hdmi/) | Out-of-tree DRM **HDMI encoder** driver. |
| [`recipes-apps/jack-panel/`](recipes-apps/jack-panel/) | The Qt 6 status panel ([`main.cpp`](recipes-apps/jack-panel/files/main.cpp)) and its systemd service. |
| [`recipes-connectivity/jack-wifi/`](recipes-connectivity/jack-wifi/) | WiFi for the RTL8188EUS: driver, firmware, `wpa_supplicant`, `networkd` DHCP. |
| [`recipes-core/`](recipes-core/), [`recipes-devtools/`](recipes-devtools/) | Build fixes for `util-linux` and `bootgen` on newer compilers. |

---

## Common tasks

All commands run from the **project root**. Settings in `conf/jack-local.conf` are picked up on the next build; you don't need to re-run kas.

**Use a different hardware design (XSA)**: edit `HDF_PATH` in `conf/jack-local.conf` and rebuild. The FSBL recipe expects the block design to be named `design_1` (it reads `design_1.hwh` from the XSA). If you change the PL address map or interrupts, update [`pynq-z2-pl.dtsi`](recipes-bsp/device-tree/files/pynq-z2-pl.dtsi) to match.

**Add a WiFi network to the image**: add these lines to `conf/jack-local.conf`:

```bitbake
WIFI_SSID = "YourNetwork"
WIFI_PSK  = "YourPassword"
```

Only the derived WPA key is stored in the image, never the plain passphrase. You can also leave these empty and add a network later on the board with `wpa_cli`.

**Add a package to the image**: add a line to `conf/layer.conf`, for example:

```bitbake
IMAGE_INSTALL:append = " nano"
```

Note the leading space inside the quotes; BitBake needs it. To find which recipe provides something, run
`kas shell sources/meta-jack/kas.yml -c "bitbake-layers show-recipes '*nano*'"`.

**Change the kernel config**: add `CONFIG_...` options to one of the `.cfg` files in [`recipes-kernel/linux/files/`](recipes-kernel/linux/files/), or create a new fragment and list it in [`linux-xlnx_%.bbappend`](recipes-kernel/linux/linux-xlnx_%25.bbappend).

**Rebuild just one piece**: much faster than a full image while you iterate.

```bash
kas shell sources/meta-jack/kas.yml -c "bitbake jack-panel"        # one recipe
kas shell sources/meta-jack/kas.yml -c "bitbake -c cleansstate jack-panel && bitbake jack-panel"  # from scratch
```

**Open an interactive build shell**: `kas shell sources/meta-jack/kas.yml` drops you into a shell where `bitbake`, `bitbake-layers` and `devtool` all work.

**Update the pinned layers**: change the `commit:` values in [`kas.yml`](kas.yml), then run `kas checkout --update sources/meta-jack/kas.yml`. Keep oe-core, meta-openembedded, meta-arm and meta-xilinx on the same release branch.

---

## How the boot works

```
SD card (FAT boot partition)
  BOOT.bin  ──►  FSBL         sets up clocks, DDR and MIO pins (ps7_init from the XSA)
                  └─► U-Boot
                        ├─ uEnv.txt  →  loads zynq-generic.bin into the FPGA (the display hardware comes alive)
                        └─ boot.scr  →  loads the kernel + system.dtb, boots Linux
Linux
  ├─ clk-dglnt-dynclk + digilent-hdmi + xlnx DRM  →  /dev/dri/card0, /dev/fb0
  ├─ systemd  →  jack-panel.service draws the Qt UI on /dev/fb0
  └─ eth0 / wlan0 (DHCP via systemd-networkd)
```

The bitstream is loaded by **U-Boot**, before Linux starts, so the video IP is already running when the kernel drivers probe. For a component-by-component tour of the video hardware (clocks, AXI memory map, EDID, data flow), see [hardware_design.md](hardware_design.md).

---

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `XSA file not found` during the build | `HDF_PATH` is missing or wrong. Set an **absolute** path in `conf/jack-local.conf`. |
| `kas checkout` fails to fetch | Network or proxy issue. kas clones over HTTPS and needs no credentials. |
| Layer/branch compatibility errors | The layer pins have been mixed. Restore [`kas.yml`](kas.yml) and run `kas checkout --update sources/meta-jack/kas.yml`. |
| Errors mentioning the FSBL, `ps7_init` or `design_1.hwh` | The XSA's Vivado version or block design name doesn't match what the FSBL recipe expects. Re-export it from a compatible Vivado. |
| BitBake complains about locale | Enable `en_US.UTF-8` in `/etc/locale.gen` and run `sudo locale-gen`. |
| Warnings about `zynq-generic` being include-only, the ZynqMP pmu-rom, or WSLv2 | Expected and harmless. |
| Build runs out of memory | Lower `BB_NUMBER_THREADS` / `PARALLEL_MAKE` in the `local_conf_header` of [`kas.yml`](kas.yml) (they default to all CPU cores). |
| Nothing on the HDMI monitor | On the board, check `cat /sys/class/fpga_manager/fpga0/state` and `dmesg \| grep -i -e drm -e hdmi -e dynclk`. Make sure the monitor is on **HDMI OUT**, not HDMI IN. |
| No serial output | Wrong COM/tty port (the board shows up as two), wrong baud rate, or the boot jumper isn't set to SD. |

---

## Security note

The image is set up for **bench development**: `root` can log in with an **empty password** (`EXTRA_IMAGE_FEATURES` in [`kas.yml`](kas.yml)). Don't connect it to an untrusted network as it is. For anything beyond the bench, remove `allow-empty-password empty-root-password allow-root-login` and set up proper users and passwords.

---

## Further reading

| Document | For |
|---|---|
| [GETTING_STARTED.md](GETTING_STARTED.md) | Detailed, step-by-step setup on a fresh machine |
| [how-bitbake-works.md](how-bitbake-works.md) | How BitBake turns this layer into an image: parsing, task graph, sstate, WIC layout |
| [hardware_design.md](hardware_design.md) | The FPGA video pipeline: components, clocks, memory map, drivers, startup sequence |
| [Yocto Project docs](https://docs.yoctoproject.org/) · [kas docs](https://kas.readthedocs.io/) · [meta-xilinx](https://github.com/Xilinx/meta-xilinx) | Upstream references |
