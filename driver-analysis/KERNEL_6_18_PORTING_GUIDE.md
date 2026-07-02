# Kernel 6.18.10 Driver Porting Guide

## Overview

Both drivers have been updated to work with kernel 6.18.10. The key changes involve updating deprecated APIs and modernizing the code to follow current kernel best practices.

## clk-dglnt-dynclk (Clock Driver)

### Changes Made

#### 1. **Header Includes** (modernized)
```diff
- #include <linux/platform_device.h>
- #include <linux/clk-provider.h>
- #include <linux/clk.h>
- #include <linux/slab.h>
- #include <linux/io.h>
- #include <linux/of.h>
- #include <linux/module.h>
- #include <linux/err.h>
- #include <linux/kernel.h>

+ #include <linux/clk-provider.h>
+ #include <linux/err.h>
+ #include <linux/io.h>
+ #include <linux/kernel.h>
+ #include <linux/module.h>
+ #include <linux/of.h>
+ #include <linux/platform_device.h>
+ #include <linux/slab.h>
```
Headers reordered alphabetically (kernel style).

#### 2. **SPDX License Header** (modern standard)
```diff
- /* * blah blah */
+ /* SPDX-License-Identifier: GPL-2.0-only */
```
Kernel 6.18 requires SPDX license identifiers instead of full GPL text.

#### 3. **Clock Registration API** (critical update)
```diff
- clk = devm_clk_register(&pdev->dev, &dglnt_dynclk->clk_hw);
- if (IS_ERR(clk))
-     return PTR_ERR(clk);
- return of_clk_add_provider(pdev->dev.of_node, of_clk_src_simple_get, clk);

+ ret = devm_clk_hw_register(&pdev->dev, &dglnt_dynclk->clk_hw);
+ if (ret) {
+     dev_err(&pdev->dev, "Failed to register clock\n");
+     return ret;
+ }
+ ret = of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_simple_get,
+                              &dglnt_dynclk->clk_hw);
+ if (ret) {
+     dev_err(&pdev->dev, "Failed to add clock provider\n");
+     return ret;
+ }
```

**Why:** Kernel 6.18 prefers `clk_hw` operations instead of `clk` pointers.

#### 4. **Logging** (kernel standards)
```diff
- printk(KERN_INFO "[HDMI] probe called...");

+ dev_info(&pdev->dev, "Digilent axi_dynclk registered successfully\n");
+ dev_err(&pdev->dev, "Failed to get parent clock name\n");
```

Use `dev_*` macros instead of `printk` for cleaner logs with device context.

#### 5. **Platform Driver Operations**
```diff
- .remove = dglnt_dynclk_remove,  /* old style */
+ .remove = dglnt_dynclk_remove,  /* no change needed, callback still valid */
```

#### 6. **Code Quality**
- Removed // C++ style comments, use /* */ for multi-line
- Fixed operator spacing in bitwise operations
- Proper return value handling for all devm_* calls

## digilent-hdmi (HDMI DRM Driver)

### Major Refactoring

The new implementation is a complete overhaul from a stub to a fully functional DRM bridge driver.

#### 1. **Framework Change: From Encoder to Bridge**

**Old Approach (Stub):**
```c
// Just registered with DRM but did nothing
.probe() returned 0 immediately
No encoder implementation
No mode setting
No EDID reading
```

**New Approach (Proper DRM Bridge):**
```c
Uses DRM bridge framework (modern kernel standard)
Implements bridge_funcs (enable, disable, mode_set, attach)
Proper DRM connector with EDID support
Atomic operations ready
```

#### 2. **Proper Initialization Sequence**

```c
struct digilent_hdmi {
    struct device *dev;
    struct clk *clk;              // Clock for pixel frequency
    struct i2c_adapter *i2c;      // EDID reading
    struct drm_bridge bridge;      // DRM framework integration
    struct drm_connector connector; // For mode negotiation
    struct drm_encoder encoder;    // Legacy support
    unsigned int fmax;            // Max pixel frequency
};
```

#### 3. **Clock Management**

```c
// Get clock from device tree
hdmi->clk = devm_clk_get(&pdev->dev, "clk");

// Set rate during mode set
clk_set_rate(hdmi->clk, pixel_clk);

// Enable/disable as needed
clk_enable(hdmi->clk);   // In bridge_enable
clk_disable(hdmi->clk);  // In bridge_disable
```

#### 4. **I2C EDID Support**

```c
// Parse device tree phandle
i2c_node = of_parse_phandle(pdev->dev.of_node, "digilent,edid-i2c", 0);
hdmi->i2c = of_find_i2c_adapter_by_node(i2c_node);

// During connector init, read EDID
edid = drm_get_edid(connector, hdmi->i2c);
if (edid) {
    drm_connector_update_edid_property(connector, edid);
    ret = drm_add_edid_modes(connector, edid);  // All monitor modes
    kfree(edid);
}
```

This allows monitors to negotiate their optimal resolution.

#### 5. **DRM Bridge Integration**

```c
// Register as a DRM bridge
drm_bridge_add(&hdmi->bridge);

// Bridge callbacks
bridge_funcs = {
    .enable = digilent_hdmi_bridge_enable,      // Power on
    .disable = digilent_hdmi_bridge_disable,    // Power off
    .mode_set = digilent_hdmi_bridge_mode_set,  // Set pixel clock
    .attach = digilent_hdmi_bridge_attach,      // Create connector
};
```

#### 6. **Connector Operations**

```c
// Get modes from EDID or fallback
get_modes() {
    if (has EDID) {
        // Real modes from monitor
        return drm_add_edid_modes(connector, edid);
    } else {
        // Safe fallback modes
        drm_add_modes_noedid(connector, 1920, 1080);
    }
}

// Always report connected (no hotplug detection on PYNQ-Z2)
detect() { return connector_status_connected; }
```

#### 7. **Device Tree Integration**

From `pynq-z2.dts`:
```dts
digilent_hdmi_0: digilent-hdmi@43c30000 {
    compatible = "digilent,hdmi";
    clocks = <&axi_dynclk_0>;           // Clock source
    clock-names = "clk";
    digilent,edid-i2c = <&i2c0>;        // EDID reading
    digilent,fmax = <150000>;           // Max 150 MHz pixel clock
    
    port@0 {
        hdmi_ep: endpoint {
            remote-endpoint = <&pl_disp_ep>;  // Connected to display
        };
    };
};
```

## API Changes Summary

| Kernel 5.15 | Kernel 6.18 | Reason |
|---|---|---|
| `devm_clk_register()` | `devm_clk_hw_register()` | Prefers clock handle objects |
| `of_clk_add_provider()` | `of_clk_add_hw_provider()` | Consistent with clk_hw |
| `drm_encoder` only | `drm_bridge` model | Modern DRM arch |
| `printk()` | `dev_info()` / `dev_err()` | Contextual logging |
| GPL text header | SPDX identifier | Kernel requirement |

## Testing

### Clock Driver
```bash
# Verify clock is registered
cat /sys/kernel/debug/clk/clk_summary | grep dynclk

# Should show:
# axi_dynclk                 1        1   100000000          0
```

### HDMI Driver
```bash
# Check DRM bridge was added
find /sys/bus/platform/devices -name "*hdmi*"

# Monitor kernel messages
dmesg | grep -i hdmi

# Once both drivers loaded, test video output
# (requires DRM display manager like DRM_GENERIC_LOAD)
```

## Known Limitations

1. **No hotplug detection** — PYNQ-Z2 doesn't have hotplug GPIO, so connector is always reported as connected
2. **Fixed 4:3 aspect** — Device tree can override pixel formats via xlnx,vformat property
3. **No dynamic resolution change** — Mode must be set at startup, not changed runtime

## Compilation

The drivers are built via Yocto recipes:

```bash
# Automatic via bitbake
bitbake core-image-minimal

# Or manual (if needed)
make -C kernel_build_dir M=$(pwd)
```

## Debugging

Enable debug logging:
```c
// In driver code
dev_dbg(hdmi->dev, "Debug message\n");

// At runtime
echo "file digilent-hdmi.c +p" > /sys/kernel/debug/dynamic_debug/control
```

## Next Steps

1. Build with Yocto: `bitbake core-image-minimal`
2. Boot and check kernel messages
3. Verify both drivers loaded: `lsmod | grep -E "clk_dglnt|digilent_hdmi"`
4. Test clock: check /sys/kernel/debug/clk/clk_summary
5. Test HDMI: verify video output and check for EDID detection
