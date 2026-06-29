# clk-dglnt-dynclk Driver Explanation

## What This Driver Does

This is a **clock driver** for the Digilent AXI Dynamic Clock IP core (`axi_dynclk`). Its job is:

1. **Provide a programmable pixel clock** for HDMI video output
2. **Integrate with Linux CCF** (Common Clock Framework) so other drivers can request any frequency they need
3. **Generate the exact frequency** the monitor and video pipeline require

```
Desired frequency (e.g., 148.5 MHz for 1920x1080@60Hz)
        ↓
[clk-dglnt-dynclk driver]
    ├─ Calculates PLL parameters
    ├─ Programs axi_dynclk registers
    └─ Produces the requested frequency
        ↓
[HDMI serializer] uses this clock for TMDS output
```

## Hardware: The axi_dynclk IP

The PYNQ-Z2 has an AXI Dynamic Clock IP block at **0x43C10000**. This IP contains a **Mixed-Mode Clock Manager (MMCM)** — a PLL (Phase-Locked Loop) that can generate many different frequencies.

```
Parent Clock (100 MHz from PS)
    ↓
[MMCM PLL]
    ├─ Input divider (curDiv)
    ├─ Feedback multiplier (fbmult)
    ├─ Output divider (clkdiv)
    └─ Main divider (maindiv)
        ↓
Output Clock (any frequency 4-800 MHz)
```

The driver's job: **calculate the right dividers so the MMCM produces the requested frequency**.

## Key Structures

### `struct dglnt_dynclk` — The Driver's State

```c
struct dglnt_dynclk {
    void __iomem *base;         // Memory-mapped base address (0x43C10000)
    struct clk_hw clk_hw;       // Linux CCF handle for this clock
    unsigned long freq;         // Currently set frequency in Hz
};
```

When userspace or another driver says "I want a 148.5 MHz clock", the CCF calls functions on this driver's `clk_hw`.

### `struct dglnt_dynclk_mode` — The Frequency Recipe

```c
struct dglnt_dynclk_mode {
    u32 freq;       // Output frequency in KHz (e.g., 148500)
    u32 fbmult;     // Feedback multiplier (2-64)
    u32 clkdiv;     // Output clock divider (1-128)
    u32 maindiv;    // Input divider (1-106)
};
```

Once we calculate the right values, this struct holds them.

### `struct dglnt_dynclk_reg` — Hardware Registers

```c
struct dglnt_dynclk_reg {
    u32 clk0L;              // Clock divider low bits
    u32 clkFBL;             // Feedback multiplier low bits
    u32 clkFBH_clk0H;       // High bits of both
    u32 divclk;             // Main divider
    u32 lockL;              // PLL lock detection config (low)
    u32 fltr_lockH;         // Filter and lock config (high)
};
```

These are the actual values written to hardware registers at:
- 0x43C10008: Clock divider
- 0x43C1000C: Feedback multiplier
- 0x43C10010: High bits
- 0x43C10014: Main divider
- 0x43C10018: Lock detection
- 0x43C1001C: Filter

## The Math: Finding the Right Frequency

### The Problem

Given:
- Parent frequency: 100 MHz (from PS clock)
- Desired output: e.g., 148.5 MHz

Find:
- `maindiv`, `fbmult`, `clkdiv` such that:

```
         Parent × fbmult
Output = ────────────────────
         maindiv × clkdiv
```

**Constraints:**
- 600 MHz ≤ VCO (before output divider) ≤ 1200 MHz
- 10 MHz ≤ PFD (reference frequency) ≤ 450 MHz
- 4 MHz ≤ Output ≤ 800 MHz
- fbmult: 2-64
- clkdiv: 1-128
- maindiv: 1-106

### The Algorithm: `dglnt_dynclk_find_mode()`

Brute-force search with constraints:

```python
for maindiv from 1 to 106:
    for fbmult from 2 to 64:
        for clkdiv from 1 to 128:
            output_freq = (parent * fbmult) / (maindiv * clkdiv)
            error = abs(output_freq - desired_freq)
            if error < best_error:
                save this as best match
```

**Why brute-force?** The constraints are tight enough that you can't solve this algebraically. You have to try possibilities and pick the closest.

```c
// Simplified version of the algorithm:
bestError = INFINITY;
for (curDiv = 1; curDiv <= maxDiv; curDiv++) {
    // curDiv = maindiv
    
    // Calculate valid range for fbmult
    minFb = curDiv * DIV_ROUND_UP(600000, parentFreq);
    maxFb = curDiv * (1200000 / parentFreq);
    
    for (curFb = minFb; curFb <= maxFb; curFb++) {
        // curFb = fbmult
        
        // Calculate best clkdiv for this fbmult
        curClkDiv = (parentFreq * curFb) / (curDiv * desiredFreq);
        
        // Clamp to valid range
        if (curClkDiv > 128) curClkDiv = 128;
        if (curClkDiv < 1) curClkDiv = 1;
        
        // Calculate actual output
        curFreq = (parentFreq * curFb) / (curDiv * curClkDiv);
        
        // Track best match
        error = abs(curFreq - desiredFreq);
        if (error < bestError) {
            bestError = error;
            save (fbmult, clkdiv, maindiv, curFreq)
        }
        
        if (error == 0) break; // Perfect match found
    }
}
```

### Units Conversion Mystery

You'll see weird conversions in `set_rate()`:

```c
rate = (rate + 100) / 200;           // Input in Hz, output in KHz × 5
parent_rate = (parent_rate + 500) / 1000;  // Input in Hz, output in KHz
```

Why? Vivado's simulation uses a divider chain with a fixed ×5 multiplier at the input. The driver abstracts this away, so userspace just says "I want 148.5 MHz" and the driver handles the conversion.

The +100 and +500 are for rounding.

## Register Programming

### Converting Parameters to Hardware Values

The `fbmult` and `clkdiv` values can't be written directly to registers. They need bit-manipulation via `dglnt_dynclk_divider()`:

```c
u32 dglnt_dynclk_divider(u32 divide)
{
    // For MMCM, dividers are encoded as high-time/low-time counters
    // Divide by N means: high-time = N/2, low-time = N/2 (rounded)
    
    highTime = divide / 2;
    
    if (divide is odd) {
        lowTime = highTime + 1;
        set wedge bit (CLK_BIT_WEDGE = 13)  // Compensation for rounding
    } else {
        lowTime = highTime;
    }
    
    // Pack into register format
    output = (highTime << 6) | lowTime;
    return output;
}
```

**Why this complexity?** MMCM hardware uses pulse-width encoding, not a simple counter. For a divide-by-7:
- High time: 3 cycles
- Low time: 4 cycles (compensated by wedge bit)
- Output: 7 cycles per input

### Lookup Tables for PLL Stability

The driver includes lookup tables (`lock_lookup`, `filter_lookup_low`) — these are **pre-calculated PLL filter coefficients** for each fbmult value (2-64). They ensure the PLL locks quickly and stably.

```c
static const u64 lock_lookup[64] = {
    0b0011000110111110100011111010010000000001,  // fbmult=2
    0b0100001000111110100011111010010000000001,  // fbmult=3
    ...
};
```

These values come from Xilinx's MMCM design spreadsheet. Different feedback multipliers need different filter settings to achieve stable locking.

## Clock Operations

The driver implements four core CCF operations:

### 1. `set_rate()` — Set a new frequency

```c
// User calls: clk_set_rate(dglnt_clk, 148500000);  // 148.5 MHz
// Linux CCF calls:
static int dglnt_dynclk_set_rate(struct clk_hw *clk_hw,
                                  unsigned long rate,
                                  unsigned long parent_rate)
{
    // Find optimal dividers for this rate
    dglnt_dynclk_find_mode(rate, parent_rate, &clkMode);
    
    // Convert to register values
    dglnt_dynclk_find_reg(&clkReg, &clkMode);
    
    // Write to hardware
    dglnt_dynclk_write_reg(&clkReg, base);
    
    // Disable, then re-enable to apply changes
    dglnt_dynclk_disable(clk_hw);
    dglnt_dynclk_enable(clk_hw);
    
    // Remember this frequency
    dglnt_dynclk->freq = clkMode.freq;
}
```

### 2. `enable()` — Turn on the clock

```c
static int dglnt_dynclk_enable(struct clk_hw *clk_hw)
{
    // Set CTRL register to 1 to enable
    writel(1, base + OFST_DISPLAY_CTRL);
    
    // Poll STATUS register until it's ready
    while (!readl(base + OFST_DISPLAY_STATUS))
        ;  // Wait for lock
    
    return 0;
}
```

The hardware sets STATUS=1 when the MMCM PLL is locked and stable.

### 3. `disable()` — Turn off the clock

```c
static void dglnt_dynclk_disable(struct clk_hw *clk_hw)
{
    // Clear CTRL register
    writel(0, base + OFST_DISPLAY_CTRL);
}
```

### 4. `recalc_rate()` — Report current frequency

```c
static unsigned long dglnt_dynclk_recalc_rate(struct clk_hw *clk_hw,
                                              unsigned long parent_rate)
{
    return dglnt_dynclk->freq;  // Just return what we set earlier
}
```

Note: This doesn't read from hardware, just returns what we calculated. If someone reprogrammed the hardware directly, this would be wrong. In practice, that doesn't happen — the driver is the only thing controlling this IP.

## Probe: Registering with Linux

When the kernel boots and finds the `axi_dynclk` node in the device tree:

```c
static int dglnt_dynclk_probe(struct platform_device *pdev)
{
    // 1. Allocate driver state
    dglnt_dynclk = devm_kzalloc(...);
    
    // 2. Map hardware registers to virtual memory
    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    dglnt_dynclk->base = devm_ioremap_resource(...);  // 0x43C10000 → kernel VA
    
    // 3. Find parent clock (PS clock, 100 MHz)
    parent_name = of_clk_get_parent_name(pdev->dev.of_node, 0);
    
    // 4. Set up CCF clock
    init.name = "axi_dynclk";
    init.ops = &dglnt_dynclk_ops;           // Enable, disable, set_rate...
    init.parent_names = &parent_name;       // Depends on PS clock
    init.num_parents = 1;
    
    // 5. Register with CCF
    clk = devm_clk_register(&pdev->dev, &dglnt_dynclk->clk_hw);
    
    // 6. Advertise as a clock provider so other drivers can use it
    return of_clk_add_provider(pdev->dev.of_node,
                               of_clk_src_simple_get, clk);
}
```

After probe, other drivers can do:

```c
// In digilent-hdmi driver:
clk = of_clk_get(pdev->dev.of_node, 0);  // Get "clk" from device tree
clk_set_rate(clk, 148500000);            // Set to 148.5 MHz
clk_enable(clk);                         // Turn it on
```

## Device Tree Integration

From your `pynq-z2.dts`:

```dts
axi_dynclk_0: axi-dynclk@43c10000 {
    compatible = "dglnt,axi-dynclk";
    reg = <0x43c10000 0x1000>;
    clocks = <&clkc 15>;           // Parent: PS clock (fclk0)
    #clock-cells = <0>;            // Provides one clock output
};

digilent_hdmi_0: digilent-hdmi@43c30000 {
    compatible = "digilent,hdmi";
    ...
    clocks = <&axi_dynclk_0>;      // Uses axi_dynclk as its pixel clock
    clock-names = "clk";
};
```

## Summary

| Concept | What It Does |
|---------|-------------|
| **Job** | Provide a programmable pixel clock via MMCM PLL |
| **Hardware** | axi_dynclk IP at 0x43C10000 |
| **Framework** | Linux CCF (Common Clock Framework) |
| **Key Operation** | Calculate MMCM dividers to achieve requested frequency |
| **Integration** | Probe finds hardware, registers with CCF, other drivers request frequencies |
| **Constraints** | Output 4-800 MHz, input divider 1-106, feedback mult 2-64, output div 1-128 |

## Testing the Driver

Once ported to kernel 6.18.10 and loaded:

```bash
# Check if clock is registered
cat /sys/kernel/debug/clk/clk_summary | grep dynclk

# Should show something like:
# axi_dynclk                 0        1   100000000          0  

# Test setting a frequency (in the driver or via sysfs)
# clk_set_rate(axi_dynclk, 148500000);  # Request 148.5 MHz
# Should see STATUS register go 0→1 (locked)
```

## Porting to Kernel 6.18.10

Key changes likely needed:
1. Update includes (some headers moved)
2. `devm_clk_register()` → may need `devm_clk_hw_register()` 
3. CCF API changes in 6.18
4. Check `clk_ops` structure (fields may have changed)
5. Test with actual hardware and monitor EDID detection

The core algorithm (finding frequency) and hardware interaction (register writes) should be unchanged.
