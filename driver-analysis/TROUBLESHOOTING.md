# Driver Troubleshooting Guide

## Symptom: Kernel Hangs After "clk_dglnt_dynclk: loading module"

### Root Causes

1. **Blocking in `of_clk_get_parent_name()`**
   - Parent clock reference not found
   - Clock framework deadlock
   
2. **Hardware access before mapping**
   - Removed in latest version (was calling `dglnt_dynclk_disable()` during probe)
   
3. **Clock registration blocking**
   - `devm_clk_hw_register()` may deadlock if CCF has issues
   - `of_clk_add_hw_provider()` may block

4. **HDMI driver autoload conflict**
   - Both drivers might be loading simultaneously and interfering

### Debugging Steps

#### Step 1: Check Device Tree

Verify axi_dynclk_0 node exists and is correct:

```bash
# On running system:
find /proc/device-tree -name "*dynclk*" -o -name "*axi-dynclk*"

# Should show: /proc/device-tree/amba/axi-dynclk@43c10000
```

Check the clocks property:
```bash
cat /proc/device-tree/amba/axi-dynclk\@43c10000/clocks
# Should reference clock controller
```

#### Step 2: Load Clock Driver in Isolation

Temporarily disable HDMI driver to test clock driver alone:

```bash
# Edit layer.conf temporarily
vi sources/meta-jack/conf/layer.conf

# Add or modify:
BBMASK += "meta-jack/recipes-modules/digilent-hdmi"

# Rebuild and test
bitbake core-image-minimal
```

#### Step 3: Monitor Kernel Logs

Boot with verbose logging:

```bash
# In GRUB/U-Boot console, add kernel parameters:
loglevel=7 ignore_loglevel

# Or edit kernel command line in device tree / grub config
```

Then check for debug messages:
```bash
[PROBE] Starting
[PROBE] Device matched
[PROBE] Memory allocated
[PROBE] Memory resource found at ...
[PROBE] Memory mapped successfully
[PROBE] Getting parent clock name...  <-- Might hang here
[PROBE] Parent clock: ...
[PROBE] Clock name: ...
[PROBE] Registering clock hardware...  <-- Or here
[PROBE] Clock registered, adding provider...
[PROBE] SUCCESS - ...
```

#### Step 4: Use Serial Console for Real-Time Output

The hang might prevent disk logging. Use serial console:

```bash
# On the board, ensure serial is enabled in device tree
# Should show boot logs in real-time

# Look for where [PROBE] messages stop
```

### Solutions by Symptom

#### Hangs at "Getting parent clock name..."

**Cause:** `of_clk_get_parent_name()` is blocking

**Fix (already applied):**
```c
parent_name = of_clk_get_parent_name(pdev->dev.of_node, 0);
if (!parent_name) {
    dev_warn(&pdev->dev, "Using fallback parent name\n");
    parent_name = "ps_clk";
}
```

If still hanging, the clock framework itself might be broken. Try:
```c
// Simpler approach - don't look up by name, just don't set parent
init.parent_names = NULL;
init.num_parents = 0;
```

This makes the clock independent, which is okay if it's not expected to track parent rate.

#### Hangs at "Registering clock hardware..."

**Cause:** `devm_clk_hw_register()` blocking

**Investigation:**
```bash
# Check if CCF is working:
cat /sys/kernel/debug/clk/clk_summary | head -50
# If this command hangs, CCF is broken

# Try booting without modules to see if core system works
```

**Fix:** Might be kernel issue with CCF. Try older/newer kernel version.

#### Hangs at "adding provider..."

**Cause:** `of_clk_add_hw_provider()` blocking

**Investigation:** Similar to above - CCF issue.

**Fix:** Try removing `of_clk_add_hw_provider()` to see if driver loads:
```c
// Temporarily comment out
// ret = of_clk_add_hw_provider(...);
```

If it loads without this, then other drivers can't find the clock (expected). This confirms the provider registration is the problem.

### Fallback: Minimal Stub Approach

If the above doesn't work, revert to absolute minimal approach:

```c
static int dglnt_dynclk_probe(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Minimal probe - just registering\n");
    platform_set_drvdata(pdev, NULL);
    return 0;  // Don't register with CCF, just load successfully
}

static void dglnt_dynclk_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Minimal remove\n");
}
```

This lets the module load. Then implement features incrementally.

### HDMI Driver Issues

If clock driver works but system still hangs, check HDMI driver:

The HDMI driver probe also has complex operations:
- `of_find_i2c_adapter_by_node()` - might block waiting for I2C
- `drm_bridge_add()` - might block if DRM framework isn't ready
- `devm_clk_get()` - depends on clock driver, could fail/hang

**To disable HDMI during testing:**
```bash
BBMASK += "meta-jack/recipes-kernel/digilent-hdmi"
```

### Testing Incremental Loading

Edit module loading order in systemd:

```bash
# Boot and drop to shell (append "rd.break" to kernel args)
# Then manually load:
modprobe clk-dglnt-dynclk
# Check dmesg for [PROBE] messages
# If successful, try:
modprobe digilent-hdmi
```

This lets you see exactly where it hangs.

### Kernel Debug Options

Enable kernel config options for better debugging:

```bash
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_SPINLOCK=y
CONFIG_DEBUG_MUTEXES=y
CONFIG_DEBUG_LOCK_ALLOC=y
CONFIG_PROVE_LOCKING=y
```

These will catch deadlocks and hang detection.

### Last Resort: Strace Equivalent

If you can get to a shell before hang:

```bash
# Monitor system calls:
sysrq-w  # Dump all tasks (Magic SysRq key)
# Shows what kernel is stuck in

# Enable Magic SysRq:
# In dts: bootargs = "... sysrq=1 ..."
```

### Expected Debug Output Sequence

With current code:
```
[PROBE] Starting
[PROBE] Device matched
[PROBE] Memory allocated
[PROBE] Memory resource found at 0x43c10000
[PROBE] Memory mapped successfully
[PROBE] Getting parent clock name...
[PROBE] Parent clock: clkc 15    (or fallback: ps_clk)
[PROBE] Clock name: axi-dynclk
[PROBE] Registering clock hardware...
[PROBE] Clock registered, adding provider...
[PROBE] SUCCESS - Digilent axi_dynclk registered
```

If you see all of these, clock driver is loaded successfully. Then check HDMI driver output.

### Next Steps

1. Rebuild with latest code (includes fixes)
2. Boot with serial console enabled
3. Watch for [PROBE] messages to identify exact hang point
4. Report which message appears last (or if any appear)
5. From there, we can apply specific fix

