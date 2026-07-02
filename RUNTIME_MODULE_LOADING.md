# Runtime Module Loading Guide

## Overview

Both PL drivers are built as kernel modules and included in the rootfs. They are **not in `KERNEL_MODULE_AUTOLOAD`**, so they won't load during boot. Load them manually when needed.

## Modules Included

- `/lib/modules/*/kernel/drivers/clk/clk-dglnt-dynclk.ko` — Clock driver
- `/lib/modules/*/kernel/drivers/gpu/drm/digilent-hdmi.ko` — HDMI driver

## Boot Process

1. System boots without loading PL drivers
2. No module-related hangs or errors
3. FPGA bitstream loads via fpga-manager
4. You manually load drivers when ready

## How to Load Drivers at Runtime

### Option 1: Load Clock Driver Only (Minimal Test)

```bash
# SSH or local console
insmod /lib/modules/$(uname -r)/kernel/drivers/clk/clk-dglnt-dynclk.ko

# Check it loaded:
lsmod | grep clk_dglnt
dmesg | grep -i dynclk
```

Expected output:
```
[PROBE] Starting
[PROBE] Device matched
...
[PROBE] SUCCESS - Digilent axi_dynclk registered
```

### Option 2: Load Both Drivers (Full HDMI)

Clock driver must load **before** HDMI driver (dependency):

```bash
# Load in order
insmod /lib/modules/$(uname -r)/kernel/drivers/clk/clk-dglnt-dynclk.ko
insmod /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/digilent-hdmi.ko

# Verify both loaded:
lsmod | grep -E "clk_dglnt|digilent"

# Check for errors:
dmesg | tail -20
```

### Option 3: Unload Drivers

```bash
# Unload in reverse order (HDMI first, then clock)
rmmod digilent_hdmi
rmmod clk_dglnt_dynclk

# Verify removed:
lsmod | grep -E "clk_dglnt|digilent"
```

## Using modprobe (Easier)

The drivers can also be loaded with `modprobe` which handles dependencies:

```bash
# Rebuild module dependency database (if needed)
depmod -a

# Load HDMI (automatically loads clock driver first)
modprobe digilent_hdmi

# Or just load clock:
modprobe clk_dglnt_dynclk

# Unload:
modprobe -r digilent_hdmi
modprobe -r clk_dglnt_dynclk
```

## Creating a Startup Service

To auto-load drivers after FPGA bitstream loads, create `/etc/systemd/system/pl-drivers.service`:

```ini
[Unit]
Description=Load PL Drivers
After=fpga-manager.service
Wants=fpga-manager.service

[Service]
Type=oneshot
ExecStart=/sbin/modprobe digilent_hdmi
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Then enable it:
```bash
systemctl enable pl-drivers.service
```

## Debugging

### Module won't load?

Check what went wrong:
```bash
dmesg | tail -50  # See kernel errors

# Or load with verbose output:
insmod -v /lib/modules/$(uname -r)/kernel/drivers/clk/clk-dglnt-dynclk.ko
```

### Where are the .ko files?

```bash
find /lib/modules -name "*dynclk*" -o -name "*digilent*"

# Or:
modinfo clk_dglnt_dynclk
modinfo digilent_hdmi
```

### Check module paths:

```bash
ls -la /lib/modules/$(uname -r)/kernel/drivers/clk/
ls -la /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/
```

## Enabling Autoload

To make drivers autoload during boot, add them to `KERNEL_MODULE_AUTOLOAD` in `layer.conf`:

```bash
# In sources/meta-jack/conf/layer.conf
KERNEL_MODULE_AUTOLOAD = "clk_dglnt_dynclk digilent_hdmi"
```

Then rebuild and redeploy.

## Current Status

✓ Drivers built and included in rootfs  
✓ Not in KERNEL_MODULE_AUTOLOAD (won't autoload)  
✓ Available to load manually anytime  
✓ Can load/unload without reboot  
✓ Simple, clean Yocto approach  

This gives maximum flexibility for debugging while preventing boot hangs.
