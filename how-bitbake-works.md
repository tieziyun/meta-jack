# How BitBake works — explained with this project

What actually happens when you run `bitbake core-image-minimal` in this repo,
from parsing to the flashable `.wic` image.

## The cast of characters

| Thing | What it is | Where it lives here |
|---|---|---|
| **BitBake** | The build engine: a Python task executor with its own config language | `sources/bitbake` |
| **OpenEmbedded-Core (oe-core)** | The base metadata: core recipes (gcc, glibc, busybox…), classes, `core-image-minimal` itself | `sources/openembedded-core/meta` |
| **Layer** | A directory of recipes/config that stacks on top of others | `meta-xilinx-core`, `meta-qt6`, `meta-jack`, … |
| **Recipe (`.bb`)** | Instructions to build one package: where to get source, how to configure/compile/install | `recipes-apps/jack-panel/jack-panel_1.0.bb` |
| **Append (`.bbappend`)** | A patch onto someone else's recipe without forking it | `recipes-kernel/linux/linux-xlnx_%.bbappend` |
| **Class (`.bbclass`)** | Reusable build logic recipes `inherit` | `qt6-cmake`, `systemd`, `kernel`, `image` |
| **kas** | A wrapper that clones the layers at pinned commits and generates the build config | `kas.yml` |

Key mental model: **BitBake doesn't know anything about compiling C code.**
It is a generic engine that (1) parses metadata into a giant variable store,
(2) computes a task dependency graph, and (3) runs tasks in parallel. All the
actual "how to build the kernel" knowledge lives in the metadata (recipes and
classes) supplied by the layers.

## Step 0: what kas set up before bitbake ever runs

You never hand-write the build config in this project; `kas build kas.yml`
(or `kas shell`) does it:

1. Clones every repo in `kas.yml` at the **pinned commit** (e.g. oe-core on
   `wrynose` @ `0f03c732`, meta-xilinx on `wrynose`). This is why the
   build is reproducible.
2. Writes `build/conf/bblayers.conf` — the layer list, one line per layer from
   the `layers:` keys in `kas.yml`.
3. Writes `build/conf/local.conf` from the `local_conf_header` blocks — that's
   where `HDF_PATH` (the Vivado XSA), `MACHINE`, and the thread counts come from.
4. Sets `MACHINE = "zynq-generic"`, `DISTRO = "nodistro"`.

So by the time you type `bitbake core-image-minimal`, the answers to "which
layers?", "which machine?", "which XSA?" are already sitting in `build/conf/`.

## Step 1: parsing — building the variable store

BitBake reads configuration in a strict order, later files able to override
earlier ones:

```
bblayers.conf  →  each layer's conf/layer.conf  →  bitbake.conf (oe-core)
→  machine conf (zynq-generic.conf from meta-xilinx-core)
→  distro conf  →  local.conf
```

Two things in this chain are worth noticing in this project:

- **`meta-jack/conf/layer.conf` is doing double duty.** Besides the normal
  layer boilerplate (`BBFILES`, `BBFILE_PRIORITY_meta-jack = "10"`), it sets
  global policy: `PREFERRED_PROVIDER_virtual/kernel = "linux-xlnx"`,
  `XILINX_WITH_ESW = "generic"`, `IMAGE_INSTALL:append`, systemd via
  `DISTRO_FEATURES`, the wks file, etc. Because layer.conf is parsed globally,
  this acts like a distro config for the whole build.
- **Layer priority** (`BBFILE_PRIORITY_meta-jack = "10"`) decides whose
  `.bbappend`s and recipes win on conflict — meta-jack's high priority means
  its kernel patches always apply on top.

Then BitBake parses **every `.bb` file in every layer** (thousands of them —
this is the "Parsing recipes" progress bar). Each recipe is parsed into its
own copy of the variable store, with matching `.bbappend`s merged in. So when
`linux-xlnx_6.x.bb` from meta-xilinx-core is parsed, meta-jack's
`linux-xlnx_%.bbappend` (`%` = any version) is glued onto the end, adding
`display.cfg`, `debug.cfg` and the DRM-whitelist patch to its `SRC_URI`.

The result is cached (`build/cache/`), which is why the second `bitbake`
invocation starts much faster.

### The variable language, in 30 seconds

```bitbake
IMAGE_INSTALL:append = " jack-panel"   # append (note the leading space)
MACHINE ?= "zynq-generic"              # default — only if not already set
HDF_PATH ?= "/home/jack/...fb.xsa"     # jack-local.conf's hard = wins over this ?=
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"  # := expands immediately
SRC_URI:append:zynq = " file://foo"    # override — only for zynq machines
```

The `:append` / `:prepend` / `:override` syntax is *the* core mechanism that
lets ten layers cooperate on one variable without editing each other's files.

## Step 2: resolving the target and building the task graph

`core-image-minimal` is just a recipe like any other —
`sources/openembedded-core/meta/recipes-core/images/core-image-minimal.bb`.
It's ~10 lines: it sets `IMAGE_INSTALL` to a minimal package set and
`inherit`s the **`image` class**, which is where all the rootfs logic lives.

BitBake resolves what the image needs:

- **`IMAGE_INSTALL`** — every package listed must be *built* first. Our
  layer.conf appended `bitstream`, `clk-dglnt-dynclk`, `digilent-hdmi`,
  `qtbase`, `jack-panel`, `htop`… each of those is a recipe with its own
  dependency tree (jack-panel `DEPENDS = "qtbase"`, qtbase needs the
  toolchain, the toolchain needs…).
- **`EXTRA_IMAGEDEPENDS`** — things the image needs but doesn't install as
  packages: we append `xilinx-bootbin virtual/dtb bitstream` so BOOT.bin and
  the DTB exist before any image task runs.
- **`virtual/*` providers** — abstract names mapped to concrete recipes.
  `virtual/kernel` → `linux-xlnx` (our `PREFERRED_PROVIDER`),
  `virtual/dtb` → `device-tree`, `virtual/boot-bin` → `xilinx-bootbin`
  (the latter two activated by `XILINX_WITH_ESW = "generic"`).
- **Multiconfig** — `BBMULTICONFIG += "fsbl-fw"` lets the FSBL build in a
  separate configuration (Cortex-A9 *standalone/baremetal* toolchain, not
  Linux userspace) inside the same bitbake invocation. That's the
  `build/conf/multiconfig/` directory.

Every recipe expands into a chain of **tasks**, roughly:

```
do_fetch → do_unpack → do_patch → do_configure → do_compile
→ do_install → do_package → do_package_write_ipk → do_populate_sysroot
```

The full build is a single directed acyclic graph of *tens of thousands* of
these tasks across all recipes. Inter-recipe edges look like: jack-panel's
`do_configure` depends on qtbase's `do_populate_sysroot` (that's what
`DEPENDS` means); the image's `do_rootfs` depends on every listed package's
`do_package_write_ipk` (that's what `RDEPENDS`/`IMAGE_INSTALL` mean).

## Step 3: execution — hashes, sstate, and parallel tasks

Before running anything, BitBake computes a **signature (taskhash)** for every
task: a hash of the task's code, its variable inputs, and the hashes of all
tasks it depends on. Then, per task:

1. **Stamp check** — did this exact task already run in `build/tmp` with the
   same hash? Skip it.
2. **Shared-state (sstate) check** — is a prebuilt result in
   `build/sstate-cache/`? Unpack it instead of building ("x tasks will run
   from sstate"). This is why touching only `jack-panel` rebuilds one recipe,
   not the toolchain.
3. Otherwise **run it**, in an isolated shell/Python environment, logging to
   `build/tmp/work/<arch>/<recipe>/<version>/temp/log.do_<task>`.

`BB_NUMBER_THREADS` (set to your core count in `kas.yml`) tasks run in
parallel; within one task, `PARALLEL_MAKE` gives make its `-j`.

The hash mechanism is also why changing a variable rebuilds exactly the right
things: edit `HDF_PATH` and the `bitstream`/`device-tree` task hashes change,
so they and everything downstream (rootfs, wic) re-run — but gcc doesn't.

### Where a task actually works

Each recipe gets a **workdir**, e.g.
`build/tmp/work/cortexa9t2hf-neon-oe-linux-gnueabi/jack-panel/1.0/`:

- `${S}` — unpacked source (for jack-panel, `S = "${WORKDIR}"` because the
  source is just local `file://` entries)
- `${B}` — build dir (cmake's binary dir)
- `${D}` — fake install root; `do_install` copies files here, and *only*
  files here end up in packages (`FILES:${PN}` decides which package)
- `recipe-sysroot/` — a private staged copy of every `DEPENDS`' headers/libs,
  so builds can't accidentally leak host or unrelated dependencies

### Following one recipe through: jack-panel

1. `do_fetch/do_unpack` — copies `main.cpp`, `CMakeLists.txt`,
   `jack-panel.service` from `meta-jack/recipes-apps/jack-panel/files/`.
2. `do_configure/do_compile` — `inherit qt6-cmake` (from meta-qt6) runs cmake
   with the Qt cross toolchain from the recipe-sysroot.
3. `do_install` — cmake install into `${D}`, plus our `do_install:append`
   adds the systemd unit; `inherit systemd` + `SYSTEMD_AUTO_ENABLE` wires up
   the enable-on-boot postinstall.
4. `do_package_write_ipk` — produces `jack-panel_1.0-r0_*.ipk` in
   `build/tmp/deploy/ipk/`, ready for the image.

And the kernel: `linux-xlnx` fetches the Xilinx tree, `do_patch` applies our
DRM-whitelist patch, then our `do_configure:append` concatenates
`display.cfg`/`debug.cfg` onto `.config` and re-runs `oldconfig` — that's the
standard Yocto config-fragment pattern.

### Sidebar: how kernel .cfg fragments actually work

These belong to the kernel's **Kconfig** system, not BitBake — BitBake only
delivers the files. Three layers:

1. **Kconfig files** in the kernel tree define the option *space*: each
   symbol's type, default, and dependencies (`depends on`, `select`).
2. **`.config`** is one concrete selection (6000+ lines), seeded from a
   defconfig.
3. **Fragments** (`display.cfg`, `debug.cfg`) are appended on top; later
   lines win. `oldconfig` then re-validates against the Kconfig rules.

Why fragments beat a full pinned .config: they state *intent* (90 readable
lines vs 6000 opaque ones), they survive kernel version bumps (our bbappend
is `linux-xlnx_%`; oldconfig re-resolves the rest each time), and they stack
per-layer like every other `:append`.

**Footgun:** `oldconfig` *silently reverts* any option whose dependencies
aren't met — set `CONFIG_DRM_XLNX=y` without `CONFIG_DRM=y` and it just
disappears. That's why display.cfg spells out the whole helper-symbol chain.
Always verify the merged result:

```sh
bitbake -c menuconfig virtual/kernel   # '/' searches a symbol; shows its
                                       # "Depends on" / "Selected by" tree
grep CONFIG_DRM_XLNX ${B}/.config      # did the fragment survive the merge?
```

At build time `.config` becomes `include/config/auto.conf` (drives
`obj-$(CONFIG_FOO) += foo.o` in kernel Makefiles — disabled code is never
compiled) and `include/generated/autoconf.h` (`#define CONFIG_FOO 1` for
`#ifdef` in C).

## Interlude: how does anyone reason about tens of thousands of tasks?

**The recipe is the grouping unit.** There is no formal "task group" feature,
but the graph naturally factors into per-recipe clusters, connected by far
fewer cross-recipe edges. In this build the clusters are, bottom to top:

```
native tools + cross-toolchain      (shared by everything)
   ├── kernel:      linux-xlnx (+ our patch/cfg fragments)
   ├── boot chain:  fsbl (multiconfig fsbl-fw) → u-boot-xlnx → bootgen
   │                  → xilinx-bootbin (BOOT.bin)
   ├── hardware:    bitstream, device-tree   (both derived from HDF_PATH)
   └── userspace:   qtbase → jack-panel, htop, libdrm, …
                        │
        core-image-minimal: do_rootfs → do_image_wic   (everything converges)
```

Kernel and U-Boot really are independent subtrees — they share only the
toolchain below and only meet again at BOOT.bin / image assembly above.
Multiconfig is the one *stronger* grouping mechanism: `fsbl-fw` is an entire
separate configuration (baremetal Cortex-A9 toolchain) whose graph touches
the main one at exactly one bridge point. You can build any cluster alone
(`bitbake u-boot-xlnx`), or visualize the clusters with
`bitbake -g core-image-minimal` → `task-depends.dot`.

**Circular dependencies are a hard error.** The task graph must be a DAG.
After constructing the runqueue — before executing anything — BitBake runs
cycle detection and aborts with the loop printed
(`Dependency loop #1 found: …`). Genuine chicken-and-egg cases are broken by
the metadata, not the engine: the compiler bootstrap is staged
(`libgcc-initial` → `glibc` → full gcc) to turn a cycle into a chain, and the
native/target recipe split serves the same purpose. Only *runtime* package
dependencies (RDEPENDS) may be mutual — the package manager can install two
packages that need each other; task-level loops never run.

**Execution is task-by-task across all recipes, not recipe-after-recipe.**
The runqueue is one flat pool: any task whose dependencies are all satisfied
is eligible, and up to `BB_NUMBER_THREADS` run concurrently — jack-panel's
`do_fetch` runs while gcc's `do_compile` is still grinding. This stays sane
because cross-recipe edges are explicit and mediated:

- Nearly every cross-recipe edge has the same shape: *my*
  `do_prepare_recipe_sysroot` depends on *your* `do_populate_sysroot` —
  that's what `DEPENDS = "qtbase"` compiles down to.
- Each recipe builds against its private `recipe-sysroot/`, assembled only
  from declared dependencies. A recipe physically cannot see a library it
  didn't declare, so concurrent unrelated builds can't interfere.
- A completed task's output is immutable (stamped + hashed); consumers copy
  from it rather than sharing live state.

## Interlude 2: "what depends on what?" — the five rules

Nobody reads the full graph; it is *generated* from a handful of local rules,
and those rules are all you think in. Every edge in the build comes from one
of these:

1. **Inside one recipe the order is fixed** — the canned chain
   (`do_fetch → … → do_package_write_ipk` / `do_populate_sysroot`) comes from
   oe-core's base class. You never wonder about intra-recipe order.
2. **`DEPENDS` = "I need your headers/libs to compile."** Always the same
   edge: my `do_prepare_recipe_sysroot` waits for your
   `do_populate_sysroot`. jack-panel's `DEPENDS = "qtbase"` means exactly
   "qtbase staged before my configure" — nothing more.
3. **`RDEPENDS` / `IMAGE_INSTALL` = "I need your finished package at rootfs
   time."** Gates `do_rootfs`, not compilation — the image waits for each
   package's `do_package_write_ipk`. This is why qtbase and htop build in
   parallel and only converge at the rootfs.
4. **Deployed artifacts gate image assembly.** Recipes producing non-package
   artifacts (BOOT.bin, bitstream, DTB) have `do_deploy`;
   `EXTRA_IMAGEDEPENDS` / `WKS_FILE_DEPENDS` make image tasks wait on them —
   that's our `EXTRA_IMAGEDEPENDS:append = " xilinx-bootbin virtual/dtb bitstream"`.
5. **The toolchain is implicit.** `bitbake.conf` prepends
   `virtual/${TARGET_PREFIX}gcc libc` to every target recipe's `DEPENDS` —
   one rule generating the thousands of edges at the bottom of the graph.

Worked example — jack-panel's timeline: before its `do_configure` can start,
only qtbase's `do_populate_sysroot` must exist (rule 2), which needed the
toolchain (rule 5). It does *not* wait for the kernel, U-Boot, or bitstream.
The only thing that waits for jack-panel is `core-image-minimal:do_rootfs`
(rule 3). Everything else proceeds in parallel, oblivious.

### Choosing DEPENDS vs RDEPENDS for your own recipe

One question, asked twice: *who needs this — the compiler on the build
machine, or the binary on the target?*

- **DEPENDS** (build time, names *recipes*): anything you `#include` or link
  against. jack-panel: `DEPENDS = "qtbase"`.
- **RDEPENDS:${PN}** (run time, names *packages*): what the program needs on
  the target beyond linked libraries.
- **Linked shared libs are auto-detected** — `do_package` objdump-scans the
  binaries and adds runtime deps for every NEEDED library ("shlibdeps").
  Never write RDEPENDS for something you link against.
- **Write manual RDEPENDS only for what objdump can't see**: `dlopen()`ed
  plugins (Qt's linuxfb platform plugin → `qtbase-plugins`), data files
  (fonts → `ttf-dejavu-sans`), programs invoked from scripts/`system()`,
  expected kernel modules.
- **Native build tools come from `inherit`** (qt6-cmake brings cmake-native
  and qtbase-native) — never DEPENDS on cmake/qmake yourself.
- **Floating dependencies** are the subtle bug: configure auto-detects a
  library that happens to be staged and silently links it, so the build
  breaks only on a clean rebuild. oe-core's answer is `PACKAGECONFIG` —
  every optional feature declared as `flag,DEPENDS,RDEPENDS`.

Failure modes are diagnostic: "header not found" at build → missing
DEPENDS; "could not load plugin" / blank text / `sh: foo: not found` on
target → missing manual RDEPENDS. Verify what you actually shipped with:

```sh
oe-pkgdata-util package-info jack-panel    # computed runtime deps
oe-pkgdata-util list-pkg-files jack-panel  # contents of the .ipk
bitbake -e jack-panel | grep '^DEPENDS='   # DEPENDS after classes added theirs
```

### Querying the graph instead of memorizing it

From `kas shell` (these parse, they don't build):

```sh
bitbake -e jack-panel | grep '^DEPENDS='       # declared build deps, expanded

bitbake -g core-image-minimal                  # writes task-depends.dot
grep '^"jack-panel' task-depends.dot           # what jack-panel waits on
grep 'jack-panel\.' task-depends.dot \
  | grep -v '^"jack-panel'                     # who waits on jack-panel

bitbake -g -u taskexp core-image-minimal       # interactive GUI graph explorer
```

`task-depends.dot` is plain text, one edge per line — grep answers "what runs
before/after X" in seconds for any X.

## Step 4: assembling the image

Once every package exists, the image recipe's own tasks run:

1. **`do_rootfs`** — creates a root filesystem in a staging dir by running
   the package manager (opkg) against `build/tmp/deploy/ipk/`, installing
   `IMAGE_INSTALL` + machine-essential packages, running postinstalls
   (e.g. enabling `jack-panel.service`). `IMAGE_BOOT_FILES` entries pull
   their providers into `/boot` here — that's how `zynq-generic.bin` (the
   bitstream) and `uEnv.txt` land in the rootfs, and how `u-boot-xlnx-uenv`
   generates the `loadfpga` command.
2. **`do_image_wic`** — because `IMAGE_FSTYPES:append = " wic wic.bmap"`,
   the wic tool partitions a disk image per `WKS_FILE = "xilinx-default-sd.wks"`:
   a FAT boot partition (populated from the rootfs's `/boot`, so BOOT.bin,
   `system.dtb`, bitstream, uEnv.txt) plus the ext4 rootfs.
3. Results are symlinked into
   `build/tmp/deploy/images/zynq-generic/core-image-minimal-zynq-generic.rootfs.wic`
   — the file you `bmaptool copy` / `dd` to the SD card.

### Anatomy of the .wic (verified against a real build)

Two partitions, exactly as `xilinx-default-sd.wks` declares:

```
core-image-minimal-zynq-generic.rootfs.wic   (MBR/dos label)
├─ p1  512 MiB   FAT32 "boot"  --active   (BootROM requires active FAT)
└─ p2  ~size-of-rootfs  ext4 "root"
```

Twist: **wic never copies boot files from deploy/** — both partitions come
from the *same* `do_rootfs` output (`--source rootfs` twice: p1 =
`--change-directory=boot`, p2 = `--exclude-path=boot/`). The files in `/boot`
were installed by *packages* during do_rootfs; `machine-xilinx-default.inc`
scans `IMAGE_BOOT_FILES` and adds the matching provider packages to
`MACHINE_ESSENTIAL_EXTRA_RDEPENDS`:

| File in p1 | Package / recipe | Built from |
|---|---|---|
| `boot.bin` | `xilinx-bootbin` (bootgen) | FSBL (multiconfig `fsbl-fw`) + `u-boot-xlnx` |
| `uImage` | `kernel-image-uimage` ← `linux-xlnx` | kernel + our bbappend |
| `system.dtb` | `device-tree` (virtual/dtb) | `pynq-z2.dts` + XSA |
| `zynq-generic.bin` | `bitstream` | `HDF_PATH` XSA |
| `uEnv.txt` | `u-boot-xlnx-uenv` | generated; scans IMAGE_BOOT_FILES to write `loadfpga` |
| `boot.scr` | `u-boot-xlnx-scr` | U-Boot script template |

p2 is everything else do_rootfs installed (the `IMAGE_INSTALL` set).
`WKS_FILE_DEPENDS` is what guaranteed all of the above finished before
`do_image_wic` started; wic then mkdosfs/mcopy's p1, mkfs.ext4's p2, writes
the MBR, and concatenates at 4 KiB alignment.

Boot-time flow (per the generated `uEnv.txt`): BootROM → `boot.bin` (FSBL)
→ U-Boot → `uEnv.txt` → `loadfpga` programs the PL → `uImage` +
`system.dtb` → kernel mounts p2 (`root=/dev/mmcblk0p2`).

Inspect any built .wic without flashing: `fdisk -l <image>.wic`, or
`wic ls <image>.wic:1` from a kas shell to list files inside partition 1.

## The whole flow in one picture

```
kas.yml ──clone/pin──▶ sources/*      jack-local.conf ─▶ HDF_PATH
                │                                          │
                ▼                                          ▼
        build/conf/{bblayers,local}.conf ◀─────────────────┘
                │
   parse: layer.conf's → machine → local.conf → all .bb + .bbappend
                │
   resolve core-image-minimal: IMAGE_INSTALL, EXTRA_IMAGEDEPENDS,
   virtual/kernel→linux-xlnx, multiconfig fsbl-fw
                │
   task graph (fetch→…→package per recipe), hashed
                │
   execute: sstate hits skip work; misses build in tmp/work/…
     ├── linux-xlnx (+ our patch & cfg fragments) → kernel + modules
     ├── device-tree (from HDF_PATH via XILINX_WITH_ESW) → system.dtb
     ├── bitstream (from the XSA) → zynq-generic.bin
     ├── fsbl (multiconfig) + u-boot → xilinx-bootbin → BOOT.bin
     └── qtbase → jack-panel → .ipk
                │
   do_rootfs (opkg installs .ipk set) → do_image_wic (xilinx-default-sd.wks)
                │
                ▼
   build/tmp/deploy/images/zynq-generic/core-image-minimal-…-.wic
```

## Handy commands for poking at all this

```sh
kas shell sources/meta-jack/kas.yml      # drop into a configured build env

bitbake core-image-minimal               # the full build
bitbake -e jack-panel | less             # final expanded variables (+ history
                                         #   comments showing which file set what)
bitbake -e | grep '^IMAGE_INSTALL='      # what's actually going in the image
bitbake -c listtasks linux-xlnx          # tasks a recipe has
bitbake -c cleansstate jack-panel        # force one recipe to rebuild
bitbake -g core-image-minimal            # dump task-depends.dot dependency graph
bitbake-layers show-layers               # layer list + priorities
bitbake-layers show-appends linux-xlnx   # which bbappends touch a recipe
oe-pkgdata-util find-path '*/uEnv.txt'   # which package owns a file
```

`bitbake -e` is the single most useful debugging tool: nearly every "why did
it do that?" is answered by the variable history it prints.
