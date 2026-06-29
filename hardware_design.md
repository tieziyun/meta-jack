# PYNQ-Z2 FPGA Video System Hardware Design

## Table of Contents

1. [What This System Does](#what-this-system-does)
2. [The Video Journey: Where Data Goes](#the-video-journey-where-data-goes)
3. [The Components: One By One](#the-components-one-by-one)
4. [How Components Connect](#how-components-connect)
5. [Physical Architecture: PS vs PL](#physical-architecture-ps-vs-pl)
6. [Memory and Bandwidth](#memory-and-bandwidth)
6. [The Drivers: Software Control](#the-drivers-software-control)
7. [AXI Memory Map and Hardware Access](#axi-memory-map-and-hardware-access)
8. [How CPU Discovers the Monitor](#how-cpu-discovers-the-monitor)
9. [Understanding Clocks](#understanding-clocks)
10. [Detailed Data Flow](#detailed-data-flow)
11. [System Startup](#system-startup)

---

## What This System Does

Your PYNQ-Z2 board can output video to an HDMI monitor. Here's what happens:

```
1. Framebuffer (picture) lives in DDR memory
2. Hardware reads the framebuffer (pixel by pixel)
3. Converts pixels to proper format
4. Adds timing signals (when to show each pixel)
5. Serializes to HDMI format
6. Sends to monitor
7. Monitor displays the image
```

**The key insight**: Everything must happen at exact frequencies because monitors are **timing-synchronous** devices. A pixel must arrive at precisely the right moment, or the monitor can't display it correctly.

---

## The Video Journey: Where Data Goes

Imagine one pixel traveling through your system. Let's call it the red pixel at position (100, 50):

```
Step 1: Living in Memory
  └─ At address 0x10000000 + (50 × 800 + 100) × 4 bytes
  └─ Stored as: R=255, G=0, B=0, A=0 (RGBA format)

Step 2: VDMA Reads It (200 MHz)
  └─ DMA engine picks it from memory
  └─ Outputs 32-bit value: 0x000000FF
  └─ Sends as continuous pixel stream

Step 3: Converter Changes Format
  └─ Takes 32-bit RGBA → 24-bit RGB
  └─ Drops the alpha byte: 0x0000FF

Step 4: Timing Controller Says "Now!"
  └─ VTC clock counter reaches pixel (100, 50)
  └─ Outputs gen_clken signal (pixel is valid)

Step 5: Video Output Holds the Pixel
  └─ Waits for VTC to say pixel time
  └─ Outputs parallel RGB: R=255, G=0, B=0
  └─ Holds for exactly 30.3 nanoseconds (one pixel period @ 33 MHz)

Step 6: HDMI Serializer Takes Over
  └─ Encodes pixel data to TMDS format
  └─ Serializes (changes parallel → serial)
  └─ Sends over twisted pair cables

Step 7: Monitor Receives and Displays
  └─ Deserializes TMDS back to RGB
  └─ Places red pixel at screen position (100, 50)
  └─ User sees the red dot on screen
```

**Total time for one pixel**: 30.3 nanoseconds (1 ÷ 33 MHz)

The entire image (800×480 pixels) flows through this pipeline, and it repeats 60 times per second.

---

## The Components: One By One

Your video system has 6 main hardware blocks. Each does one specific job.

### Component 1: VDMA (Video DMA Engine)

**Job**: Read pixels from RAM and output them as a stream

**Address**: Memory-mapped at 0x43000000

**Think of it as**: A dedicated pixel reader that doesn't bother the CPU

```
What it does:
  ├─ You tell it: "Read 800×480 pixels from address 0x10000000"
  ├─ It starts reading continuously
  ├─ Outputs one pixel every clock cycle (@ 200 MHz)
  ├─ When it reaches the end, loops back to the start
  └─ Repeats forever (or until you turn it off)

Why we need it:
  ├─ CPU is slow at reading pixels (only ~650 million ops/sec)
  ├─ Video needs 23 million pixels/sec (92 MB/s total)
  ├─ CPU would spend 28% of its time just reading pixels
  ├─ VDMA does it for free (hardware)
  └─ CPU can do other things
```

### Component 2: AXI Stream Converter

**Job**: Change pixel format from 32-bit to 24-bit

**Think of it as**: A data format translator

```
Input:  32-bit pixels (RGBA format)
        ├─ Byte 0: Red (0-255)
        ├─ Byte 1: Green (0-255)
        ├─ Byte 2: Blue (0-255)
        └─ Byte 3: Alpha (unused)

Output: 24-bit pixels (RGB format)
        ├─ Byte 0: Red (0-255)
        ├─ Byte 1: Green (0-255)
        └─ Byte 2: Blue (0-255)

What it does: Strips the alpha byte
  Example: {255, 0, 0, 0} → {255, 0, 0}
```

### Component 3: Video Output IP

**Job**: Take RGB pixels and add sync signals

**Think of it as**: The adapter between pixel data and display signals

```
Inputs:
  ├─ Pixel stream (RGB values, 24-bit)
  └─ Timing signals from VTC (when to show pixels)

Outputs:
  ├─ Red channel (8 bits)
  ├─ Green channel (8 bits)
  ├─ Blue channel (8 bits)
  ├─ Horizontal sync (hsync)
  └─ Vertical sync (vsync)

What it does:
  ├─ Waits for VTC to say "show a pixel now"
  ├─ Holds the RGB value on output lines
  ├─ Keeps it steady for one pixel period
  └─ Includes sync signals so display knows row/frame boundaries
```

### Component 4: VTC (Video Timing Controller)

**Job**: Generate the heartbeat of video (pixel clock, line clock, frame clock)

**Address**: Memory-mapped at 0x43C00000

**Think of it as**: The metronome that keeps everything in sync

```
Input:
  └─ Pixel clock (33 MHz for 800×480)

Outputs:
  ├─ gen_clken: "Pixel time - show this value now"
  ├─ hsync: "End of line, wrap to next line"
  ├─ vsync: "End of frame, wrap to first line"
  └─ Pixel/line counters

What it does:
  ├─ Counts at 33 MHz (one count every 30.3 ns)
  ├─ After 800 counts: Asserts hsync (line complete)
  ├─ After 480 lines: Asserts vsync (frame complete)
  ├─ Generates gen_clken when in active pixel area
  └─ Stays quiet during blanking (no pixel data sent)

Analogy: Like sheet music
  ├─ Pixel clock = tempo (beats per minute)
  ├─ hsync = end of measure
  ├─ vsync = end of section
  └─ gen_clken = "play a note now"
```

### Component 5: axi_dynclk_0 (Dynamic Clock Generator)

**Job**: Generate the exact pixel clock frequency needed

**Address**: Memory-mapped at 0x43C10000

**Think of it as**: A frequency synthesizer (like radio tuner)

```
Input:
  └─ Reference clock: 100 MHz from ARM system

Outputs:
  ├─ Pixel clock (33 MHz for 800×480, varies by resolution)
  └─ 5× clock (165 MHz for 800×480, always 5× pixel clock)

How it works:
  ├─ Inside: Phase-Locked Loop (PLL)
  ├─ Multiplies and divides the input clock
  ├─ Compares output to expected frequency
  ├─ Tunes until they match
  ├─ LOCKED signal goes high when stable
  └─ All done in microseconds

Why we need it:
  ├─ Different resolutions need different pixel clocks
  ├─ 800×480 @ 60Hz = 33 MHz
  ├─ 1920×1080 @ 60Hz = 148.5 MHz
  ├─ Can't hardcode to one frequency
  └─ Need programmable clock generator
```

### Component 6: RGB2DVI (HDMI Serializer)

**Job**: Convert parallel RGB into HDMI serial format

**Think of it as**: The HDMI encoder

```
Inputs:
  ├─ Parallel RGB (8 bits each: Red, Green, Blue)
  ├─ Hsync and Vsync signals
  ├─ Pixel clock (33 MHz)
  └─ 5× pixel clock (165 MHz)

Processing:
  ├─ Encodes RGB to TMDS format (adds parity/error correction)
  ├─ Serializes parallel bits into serial stream
  ├─ Uses 5× clock to shift bits out fast
  └─ Creates differential signals (noise-immune)

Outputs:
  ├─ 3 twisted pairs (for R, G, B channels)
  ├─ 1 twisted pair (for TMDS clock)
  └─ Plugs directly into HDMI connector

Why 5× clock?
  ├─ TMDS encodes 8 bits into 10 bits per pixel
  ├─ Need to serialize 10 bits per pixel period
  ├─ One pixel period @ 33 MHz = 30.3 ns
  ├─ To shift 10 bits in 30.3 ns: need 5× clock
  └─ 5 cycles × 2 bits/cycle = 10 bits per pixel
```

---

## How Components Connect

### The Signal Flow Diagram

```
┌──────────────────────────────────────┐
│ DDR3 Memory                          │
│ (Framebuffer at 0x10000000)         │
└────────────┬─────────────────────────┘
             │ 200 MHz data bus
             ↓
┌──────────────────────────────────────┐
│ VDMA (Video DMA)                     │
│ Reads pixels, outputs AXI Stream    │
└────────────┬─────────────────────────┘
             │ 200 MHz AXI Stream
             │ (32-bit RGBA pixels)
             ↓
┌──────────────────────────────────────┐
│ Subset Converter                     │
│ Strips alpha: RGBA → RGB            │
└────────────┬─────────────────────────┘
             │ 200 MHz AXI Stream
             │ (24-bit RGB pixels)
             ↓
         [FIFO Buffer with clock crossing]
             │
             ↓ 33 MHz pixel clock domain
┌──────────────────────────────────────┐
│ Video Output IP                      │
│ Holds pixels on output when valid    │
└────────────┬─────────────────────────┘
             │
             ├─→ [Timing signals from VTC]
             │   (gen_clken, hsync, vsync)
             │
             └─→ Parallel RGB output
                 (24-bit: R, G, B)
             │
             ↓
┌──────────────────────────────────────┐
│ RGB2DVI (HDMI Serializer)            │
│ Converts parallel → HDMI serial      │
│ Uses: Pixel clock + 5× clock        │
└────────────┬─────────────────────────┘
             │
             ↓
         [HDMI Connector]
             │
             ↓
        [Monitor Display]

Separate Blocks (not in main flow):
  ├─ VTC: Generates timing signals (33 MHz)
  ├─ axi_dynclk_0: Generates pixel clock (33 MHz, 165 MHz)
  └─ I2C (for monitor EDID reading)
```

### Clock Distribution

```
From PS7 (ARM):
  ├─ 100 MHz → axi_dynclk_0 (reference input)
  ├─ 100 MHz → ARM control plane
  └─ 200 MHz → VDMA data plane

From axi_dynclk_0:
  ├─ 33 MHz (pixel clock) → VTC, Video Output IP, RGB2DVI
  └─ 165 MHz (5× clock) → RGB2DVI serialization
```

### Memory and Configuration Buses

```
ARM CPU (via AXI Lite @ 100 MHz):
  ├─ Programs VDMA (tells it which framebuffer, dimensions)
  ├─ Programs VTC (tells it pixel/line/frame timing)
  └─ Programs axi_dynclk_0 (tells it what frequency)

ARM CPU (via I2C @ 100 kHz):
  └─ Reads monitor EDID (discovers monitor capabilities)

DDR Memory:
  ├─ Stores framebuffer (read by VDMA @ 200 MHz)
  └─ High-performance port (no CPU contention)
```

---

## Physical Architecture: PS vs PL

### Understanding the Two Subsystems

Zynq-7000 is not a single monolithic system. It contains two distinct processing environments on the same chip:

**PS (Processing System)**: Fixed hardware
- ARM Cortex-A9 dual-core CPU
- DDR3 memory controller (hardwired)
- Peripherals: UART, USB, Ethernet, I2C, GPIO, etc.
- Designed by Xilinx, cannot be changed
- You run Linux here

**PL (Programmable Logic)**: FPGA fabric
- Reconfigurable logic (LUTs, flip-flops, RAM)
- User-placed IP cores (VDMA, VTC, RGB2DVI)
- Can be changed for each project
- You can design custom hardware here

### Where Does Everything Live?

```
┌──────────────────────────────────────────────────────────┐
│ Zynq-7000 SoC                                            │
│                                                          │
│  ┌────────────────────────────────┐                     │
│  │ PS (Processing System)         │                     │
│  │ HARDWIRED (fixed by Xilinx)    │                     │
│  │                                │                     │
│  │  ┌──────────────────────────┐  │                     │
│  │  │ ARM Cortex-A9 Cores      │  │                     │
│  │  └──────────────────────────┘  │                     │
│  │                                │                     │
│  │  ┌──────────────────────────┐  │                     │
│  │  │ DDR3 Controller (PS)     │  │ ◄── Fixed hardware
│  │  │ (hardwired to DDR3)      │  │                     │
│  │  └──────────────────────────┘  │                     │
│  │           ▲                     │                     │
│  │           │ DDR3 bus           │                     │
│  │           │ (wires)            │                     │
│  │           ▼                     │                     │
│  │  ┌──────────────────────────┐  │                     │
│  │  │ DDR3 Memory (512 MB)      │  │                     │
│  │  └──────────────────────────┘  │                     │
│  └────────────────────────────────┘                     │
│           ▲                                              │
│           │ HP Port (AXI physical connection)           │
│           │                                              │
│  ┌────────┴─────────────────────────┐                   │
│  │ PL (Programmable Logic)          │                   │
│  │ YOUR FPGA DESIGN (reconfigurable)│                   │
│  │                                  │                   │
│  │  ┌────────────────────────────┐  │                   │
│  │  │ VDMA (user-placed IP)      │  │ ◄── Can be replaced
│  │  │ (reads pixels)             │  │     with different design
│  │  └────────────┬───────────────┘  │                   │
│  │               │ AXI output       │                   │
│  │               ▼                  │                   │
│  │  ┌────────────────────────────┐  │                   │
│  │  │ VTC (user-placed IP)       │  │                   │
│  │  │ (timing signals)           │  │                   │
│  │  └────────────────────────────┘  │                   │
│  │               │                  │                   │
│  │  ┌────────────▼───────────────┐  │                   │
│  │  │ RGB2DVI (user-placed IP)   │  │                   │
│  │  │ (HDMI serializer)          │  │                   │
│  │  └────────────────────────────┘  │                   │
│  │               │                  │                   │
│  │  ┌────────────▼───────────────┐  │                   │
│  │  │ AXI Interconnect (PL)      │  │                   │
│  │  │ Routes requests to PS      │  │                   │
│  │  └────────────┬───────────────┘  │                   │
│  └───────────────┼──────────────────┘                   │
│                  │ AXI signals cross boundary            │
│                  │                                       │
└──────────────────┼──────────────────────────────────────┘
                   │
            (HP Port: physical connection)
```

### How VDMA Reaches DDR3 Controller

VDMA lives in PL (FPGA fabric), but DDR3 controller is in PS. They're physically separated. Here's how VDMA reads pixels:

```
Step 1: VDMA Makes Request
  VDMA (in PL): "I want pixels from address 0x10000000"
  └─ Outputs: Address and read request on AXI output port

Step 2: AXI Interconnect in PL Routes It
  AXI Interconnect (in PL): "This request goes to PS"
  └─ Routes VDMA's request toward PS boundary

Step 3: Cross the Boundary (HP Port)
  HP Port (physical wires between PL and PS):
  ├─ Carries AXI signals: Address, Read/Write, Data
  ├─ Crosses the boundary between FPGA fabric and PS
  └─ Only way for PL to reach PS hardware

Step 4: AXI Interconnect in PS Routes It
  AXI Interconnect (in PS): "This goes to DDR3 controller"
  └─ Routes VDMA's request to DDR3 controller

Step 5: DDR3 Controller Processes It
  DDR3 Controller (in PS):
  ├─ Receives: "Read 4 bytes from 0x10000000"
  ├─ Accesses DDR3 memory
  └─ Returns: 4 bytes of pixel data

Step 6: Data Returns
  DDR3 Controller → AXI (PS) → HP Port → AXI (PL) → VDMA
  └─ VDMA receives pixel data and outputs to next stage
```

### Why Separate PS and PL?

```
Good engineering design reasons:

1. Isolation
   ├─ FPGA cannot accidentally corrupt ARM kernel
   ├─ ARM can protect itself from faulty FPGA designs
   └─ Each system protected from the other

2. Flexibility
   ├─ You can change FPGA design (rebuild PL)
   ├─ ARM side (PS) stays the same
   ├─ Can use different FPGA designs without changing OS
   └─ Good for development and deployment

3. Specialization
   ├─ PS optimized for general-purpose computing (ARM)
   ├─ PL optimized for parallel logic (FPGA)
   ├─ DDR3 controller stays with ARM (makes sense)
   └─ Each side does what it's good at

4. Standardization
   ├─ AXI is industry standard (ARM designed)
   ├─ Any PL device can talk to PS via AXI
   ├─ Not vendor-specific or proprietary
   └─ Easier to learn and debug
```

### Why Can't PL Just Have Direct Memory Access?

```
Could Xilinx put DDR3 controller in PL?

Theoretically: Yes
Practically: No, bad engineering

Problems:
  ├─ DDR3 controller is complex (uses lots of FPGA logic)
  ├─ Would leave less FPGA space for user designs
  ├─ Would duplicate functionality (two controllers = confusing)
  ├─ ARM couldn't access same DDR3 (separate memory spaces)
  ├─ Would require users to rebuild DDR3 controller code
  └─ Defeats entire purpose of having both PS and PL

Current design is much better:
  ├─ PS has ONE DDR3 controller (shared with PL via HP port)
  ├─ PL is 100% available for user logic
  ├─ Both systems access same memory coherently
  ├─ No duplication, clean architecture
  └─ This is how modern SoCs are designed
```

### The HP Port: The Bridge

```
The HP (High-Performance) port exists because:

1. Physical separation
   └─ PL and PS are different silicon blocks
   └─ They need a physical connection
   └─ HP port is that connection

2. Real-time access
   └─ HP port guarantees bandwidth for VDMA
   └─ Prevents CPU from starving video streaming
   └─ More important than just "having a connection"

3. Protocol translation
   └─ PL speaks AXI (its native language)
   └─ PS speaks AXI (its native language)
   └─ HP port carries AXI signals directly
   └─ No conversion needed, just a physical wire bridge

Without HP port:
  └─ PL devices have NO way to access PS hardware
  └─ Video system cannot work at all

With GP port only:
  └─ PL and CPU would compete for bandwidth
  └─ Video would stutter when CPU is busy
  └─ Unacceptable for real-time video
```

---

## Memory and Bandwidth

### How Much Bandwidth Do We Need?

For video to work, we need to move pixels from memory to display at a certain rate.

#### 800×480 @ 60 Hz

```
Pixels per second:
  800 pixels/line × 480 lines/frame × 60 frames/second
  = 23,040,000 pixels/second

Bytes per pixel:
  RGBA format = 4 bytes per pixel

Total bandwidth needed:
  23,040,000 pixels × 4 bytes = 92,160,000 bytes/second
  = 92.16 MB/s

DDR3 available:
  ~5000 MB/s

Utilization:
  92 / 5000 = 1.8% (HUGE headroom!)
```

#### Higher Resolutions

| Resolution | Refresh | Required | Available | Headroom |
|-----------|---------|----------|-----------|----------|
| 800×480 | 60 Hz | 92 MB/s | 5000 MB/s | 98% |
| 1920×1080 | 60 Hz | 498 MB/s | 5000 MB/s | 90% |
| 4K | 60 Hz | 1990 MB/s | 5000 MB/s | 60% |

**Conclusion**: Memory bandwidth is **never** the bottleneck. Your system can handle 4K video from bandwidth perspective. The bottleneck is always the **pixel clock** (determined by HDMI/display specifications).

### Why VDMA Instead of CPU Reading?

```
If CPU read pixels directly:
  ├─ Must execute 384,000 loop iterations per frame
  ├─ At 60 fps: 23 million loops per second
  ├─ Each loop: ~8 CPU cycles
  ├─ Total: 184 million cycles/sec
  ├─ ARM available: 650 million cycles/sec
  ├─ CPU overhead: 184/650 = 28% of CPU!
  └─ System becomes sluggish (network/USB slow)

With VDMA:
  ├─ Hardware reads pixels automatically
  ├─ CPU overhead: 0%
  ├─ CPU can do other work
  └─ System stays responsive
```

### Memory Port Architecture

DDR memory has multiple access ports:

```
PS7 (ARM System):
  ├─ M_AXI_GP0: General-purpose master (CPU)
  │   ├─ Typical use: 50-200 MB/s
  │   └─ Variable (depends on what CPU is doing)
  │
  └─ S_AXI_HP0: High-performance slave (VDMA)
      ├─ Typical use: 92-500 MB/s
      └─ Predictable (guaranteed for video)

Benefit: Separate ports
  ├─ CPU access doesn't interfere with VDMA
  ├─ VDMA has guaranteed bandwidth
  └─ Video never stutters because CPU is busy
```

---

## The Drivers: Software Control

Hardware components don't do anything by themselves. Linux kernel drivers control them. Your video system uses two main drivers.

### Driver 1: clk-dglnt-dynclk (Clock Provider)

**Purpose**: Make the pixel clock programmable

**What it controls**: axi_dynclk_0 (the dynamic clock generator)

**Location**: `drivers/clk/clk-dglnt-dynclk.c` (in kernel)

**What it does**:

```
1. Registers axi_dynclk_0 as a clock source
   └─ Makes it discoverable by other drivers via Linux CCF (Common Clock Framework)

2. Listens for clock requests
   └─ Example: "I need 148.5 MHz for 1920×1080"

3. Calculates PLL parameters (M and D)
   └─ For 148.5 MHz from 100 MHz input: M=59, D=20

4. Programs axi_dynclk_0 registers
   └─ Writes M and D values to memory addresses
   └─ Tells it to enable

5. Waits for lock
   └─ Polls LOCKED signal until stable
   └─ Returns success once frequency is correct
```

**How it's used**:

```
digilent-hdmi driver says:
  "Give me 148.5 MHz pixel clock"
  ↓
clk-dglnt-dynclk driver:
  ├─ Calculates: M=59, D=20
  ├─ Writes to axi_dynclk_0 @ 0x43C10000
  ├─ Waits for LOCKED
  └─ Returns: "148.5 MHz ready"
  ↓
digilent-hdmi driver:
  └─ Can now proceed with video setup
```

**Key point**: This driver provides a standard Linux interface to the hardware clock generator. Other drivers don't need to know about axi_dynclk_0 directly; they just ask the clock framework for a frequency.

---

### Driver 2: digilent-hdmi (Display Output)

**Purpose**: Provide HDMI output to the Linux display system

**What it controls**: 
- axi_dynclk_0 (via clk-dglnt-dynclk)
- v_tc_0 (timing controller)
- rgb2dvi_0 (HDMI serializer)
- i2c0 (for EDID reading)

**Location**: `drivers/gpu/drm/digilent-hdmi.c` (in kernel)

**What it does**:

```
1. Probes the device (during startup)
   ├─ Checks if HDMI hardware exists in device tree
   └─ Initializes driver structures

2. Reads monitor EDID via I2C
   ├─ Sends I2C request to monitor
   ├─ Gets 128 bytes of capability data
   ├─ Parses supported resolutions
   └─ Makes this list available to Linux desktop

3. Registers as a DRM encoder
   ├─ Tells Linux: "I can output video"
   ├─ Tells Linux: "These are my supported resolutions"
   └─ Provides callbacks for mode setting

4. When user selects a resolution (atomic_mode_set callback)
   ├─ Gets timing parameters from EDID
   ├─ Requests pixel clock from clk-dglnt-dynclk
   ├─ Programs VTC with timing parameters
   ├─ Configures VDMA (via software or hardware)
   └─ Enables video output

5. Handles cleanup when needed
   ├─ Disables clocks
   ├─ Resets hardware
   └─ Unregisters from DRM
```

**How it's used**:

```
User (desktop environment) says:
  "Set display to 1920×1080@60Hz"
  ↓
Linux DRM framework:
  └─ Calls digilent-hdmi atomic_mode_set callback
  ↓
digilent-hdmi driver:
  ├─ Looks up 1920×1080 timing in EDID data
  │   └─ Pixel clock: 148.5 MHz
  │   └─ H active: 1920, H blanking: 280
  │   └─ V active: 1080, V blanking: 45
  │
  ├─ Calls clk_set_rate(hdmi_clk, 148500000)
  │   └─ clk-dglnt-dynclk sets axi_dynclk_0 to 148.5 MHz
  │
  ├─ Writes to VTC registers (@ 0x43C00000)
  │   ├─ H_ACTIVE = 1920
  │   ├─ H_BLANKING = 280
  │   ├─ V_ACTIVE = 1080
  │   ├─ V_BLANKING = 45
  │   └─ Sync parameters
  │
  └─ Returns success
  ↓
Linux DRM framework:
  └─ Desktop now displays 1920×1080
```

**Key callbacks** (functions digilent-hdmi provides):

```
.atomic_check:
  └─ "Can you display this mode?"
  └─ Returns success or error

.atomic_mode_set:
  └─ "Set up for this mode"
  └─ Programs clock, VTC, etc.
  └─ Returns success or error

.atomic_enable:
  └─ "Turn on video output"
  └─ Enables clocks and hardware

.atomic_disable:
  └─ "Turn off video output"
  └─ Disables clocks and hardware
```

---

### How the Two Drivers Work Together

```
digilent-hdmi needs a pixel clock
  ↓
digilent-hdmi requests from Linux clock framework:
  clk_set_rate(hdmi_clk, 148500000)
  ↓
Linux clock framework routes to clk-dglnt-dynclk
  ↓
clk-dglnt-dynclk:
  ├─ Calculates PLL parameters
  ├─ Programs axi_dynclk_0
  ├─ Waits for lock
  └─ Returns success
  ↓
digilent-hdmi:
  ├─ Clock is now stable at 148.5 MHz
  ├─ Programs VTC with timing
  └─ Video output begins
```

### Key Difference from Hardware

The drivers **don't** directly handle video streaming:

```
What drivers DO:
  ├─ Initialize hardware
  ├─ Program registers
  ├─ Handle clock requests
  ├─ Configure timing
  └─ Manage EDID reading

What drivers DON'T:
  ├─ Read pixels from memory (VDMA does this automatically)
  ├─ Generate timing signals (VTC does this automatically)
  ├─ Serialize video (RGB2DVI does this automatically)
  └─ Send pixels to HDMI (hardware does this automatically)

Once configured, hardware runs independently:
  ├─ CPU doesn't need to copy pixels
  ├─ CPU doesn't need to generate timing
  ├─ Pixel clock runs continuously
  └─ Video flows forever (or until disabled)
```

---

## AXI Memory Map and Hardware Access

Hardware components aren't "computers" - they're just circuits. The only way to talk to them is by reading and writing to memory addresses. This is called **memory-mapped I/O**.

### What is AXI Lite?

**AXI Lite** is a standard protocol that allows the CPU to communicate with hardware components. Think of it like a post office:

```
CPU (wants to talk to hardware):
  "I want to write the value 5 to device X"
  ↓
AXI Lite bus (carries the message):
  ├─ Address: "Where is device X?"
  ├─ Data: "What value? 5"
  ├─ Write signal: "This is a write operation"
  └─ Connects to: Device X
  ↓
Device X (receives the message):
  └─ Sees: "Someone wrote 5 to my register"
  └─ Action: "I'll change my behavior based on this"
```

### The Memory Map: Where Each Component Lives

Your FPGA system maps each hardware component to a specific address range. When the CPU writes to that address, the message goes to that component.

#### Video System Memory Map

```
Address Range      Component              Purpose
─────────────────────────────────────────────────────────────
0x40000000-0x4FFFFFFF  FPGA Fabric (PL)
  │
  ├─ 0x41200000       axi_gpio_0         General Purpose GPIO
  │  └─ Size: 64 KB
  │
  ├─ 0x43000000       axi_vdma_0         Video DMA Controller
  │  └─ Size: 64 KB
  │  └─ Controls pixel reading from memory
  │
  ├─ 0x43C00000       v_tc_0             Video Timing Controller
  │  └─ Size: 64 KB
  │  └─ Controls HSYNC, VSYNC, pixel timing
  │
  ├─ 0x43C10000       axi_dynclk_0       Dynamic Clock Generator
  │  └─ Size: 64 KB
  │  └─ Controls pixel clock frequency
  │
  └─ 0x43C20000-0x43C30000  Other display IP (in design)
     └─ Reserved for future expansion

0xE0000000-0xEFFFFFFF  PS (Processing System - ARM)
  │
  ├─ 0xE0000000       UART0              Serial console
  ├─ 0xE0004000       I2C0               Monitor EDID reading
  ├─ 0xE0102000       USB0               USB Host
  ├─ 0xE000A000       GPIO0              General Purpose GPIO
  ├─ 0xE000B000       Ethernet (GEM0)    Network
  ├─ 0xE0100000       SDHCI0             SD Card
  │
  └─ ... other PS peripherals

0x00000000-0x20000000  DDR3 Memory
  │
  ├─ 0x00000000-0x00100000  Reserved (kernel)
  │
  ├─ 0x10000000       Framebuffer (example)
  │  └─ Size: 1.5 MB (for 800×480 RGBA)
  │  └─ Store: Picture pixels
  │
  └─ 0x10200000+      Application memory
     └─ User programs, heap, stack, etc.
```

### How CPU Reads/Writes via AXI Lite

The CPU talks to hardware by:
1. **Knowing the address** where the register is
2. **Knowing the data width** (usually 32 bits)
3. **Executing a read or write instruction**

#### Example 1: Writing to axi_dynclk_0

```
CPU wants to: Set pixel clock multiplier M = 59

C code:
  iowrite32(59, base_address + 0x10);
  
What happens:
  ├─ CPU executes write instruction
  ├─ Address: 0x43C10000 + 0x10 = 0x43C10010
  ├─ Data: 59 (in 32-bit format)
  ├─ AXI Lite bus carries this to axi_dynclk_0
  └─ axi_dynclk_0 receives and stores in CLKFBOUT_MULT register

Result:
  └─ axi_dynclk_0 uses M=59 in its PLL calculations
```

#### Example 2: Reading from axi_dynclk_0

```
CPU wants to: Check if pixel clock is locked

C code:
  status = ioread32(base_address + 0x08);
  if (status & LOCKED_BIT) {
    // Clock is stable
  }
  
What happens:
  ├─ CPU executes read instruction
  ├─ Address: 0x43C10000 + 0x08 = 0x43C10008
  ├─ AXI Lite bus queries axi_dynclk_0
  └─ axi_dynclk_0 responds with STATUS register value

Result:
  └─ CPU reads register value and checks LOCKED bit
```

### Why Memory Mapping is Needed

Memory mapping is the bridge between software and hardware:

```
Software (CPU) ← Memory-Mapped I/O → Hardware (Circuits)
    ↓                                     ↓
Reads/writes                      Changes behavior
to memory                          based on what
addresses                          it reads
```

Without memory mapping:
- CPU has no way to talk to hardware
- Hardware has no way to receive commands
- Video system cannot be controlled

With memory mapping:
- CPU writes control values to memory addresses
- Hardware reads those addresses and responds
- Entire video system can be programmed and controlled

---

## How CPU Discovers the Monitor

When you plug in an HDMI monitor, the monitor itself contains information about what it can display. The CPU reads this and configures the system accordingly.

### Step 1: Monitor EDID (What the Monitor Says)

Inside every HDMI monitor is a small memory chip (EEPROM) that stores information:

```
"I'm a 1920×1080 display"
"I can show these resolutions:
  - 640×480 @ 60 Hz
  - 800×600 @ 60 Hz
  - 1024×768 @ 60 Hz
  - 1920×1080 @ 60 Hz (preferred)"
"My pixel clock for 1920×1080 is 148.5 MHz"
"Horizontal blanking is 280 pixels"
"Vertical blanking is 45 lines"
```

This information is called **EDID** (Extended Display Identification Data).

### Step 2: Reading EDID Over I2C

The monitor stores EDID at **I2C address 0xA0**. ARM can read it via the I2C bus (which is part of HDMI):

```
HDMI Connector includes:
  ├─ TMDS pins (video data)
  ├─ Clock pins
  └─ DDC (I2C bus for communication)
      ├─ SCL (clock line)
      └─ SDA (data line)

ARM reads EDID:
  1. Sends: "I2C address 0xA0, please send 128 bytes"
  2. Monitor responds: [128 bytes of EDID data]
  3. Takes ~10 milliseconds
  4. Happens during startup (not real-time critical)
```

### Step 3: Parsing EDID

The 128 bytes contain encoded information. Linux driver parses it to find:
- Supported resolutions
- Preferred resolution
- Detailed timing for each resolution (pixel clock, sync timings, blanking)

### Step 4: Using EDID to Configure Hardware

When user selects a resolution:

```
User selects: 1920×1080@60Hz
  ↓
Linux DRM system looks up timing from EDID
  ├─ Pixel clock: 148.5 MHz
  ├─ H active: 1920, H blanking: 280
  ├─ V active: 1080, V blanking: 45
  ├─ Hsync/Vsync offsets and widths
  ↓
digilent-hdmi driver programs hardware
  ├─ Requests clock: clk_set_rate(hdmi_clk, 148500000)
  ├─ Programs VTC with all timing parameters
  ├─ Waits for LOCKED signal (clock stable)
  ↓
Video pipeline ready
  ├─ Pixels flowing at 148.5 MHz rate
  └─ Monitor sees correct timing
```

**Key insight**: Without EDID, ARM wouldn't know what the monitor can display. With EDID, ARM configures itself automatically to match the monitor's expectations.

---

## Understanding Clocks

Your system uses multiple clock frequencies, each optimized for its specific function. Now that you understand what the monitor needs, you can understand why these clocks exist.

### Clock #1: FCLK_CLK0 @ 100 MHz (ARM Control)

**Where it goes**: ARM processor, configuration registers, I2C

**Why 100 MHz**: Fast enough for register programming, no need for more

**Tolerance**: ±10% is okay (even 90-110 MHz works fine)

**Why forgiving**: Configuration happens once during startup, then clock barely matters

### Clock #2: FCLK_CLK1 @ 200 MHz (Video Data)

**Where it goes**: VDMA, stream converters, data movement

**Why 200 MHz**: Fast enough to deliver 92 MB/s of pixel data with headroom

**Tolerance**: ±1% is okay

**Why forgiving**: FIFO buffer between this clock and pixel clock; buffer absorbs timing variations

### Clock #3: Pixel Clock (Programmable)

**Frequency depends on monitor**: 33 MHz for 800×480, 148.5 MHz for 1920×1080, etc.

**Where it goes**: VTC (timing), Video Output IP, display synchronization

**Why this frequency**: Display requires specific pixel rate based on resolution

**Tolerance**: ±0.5% maximum (must be exact)

**Why strict**: Display is synchronous device; pixels must arrive at precise moments

**Generated by**: axi_dynclk_0 PLL (locked to ±0.1% automatically)

**Why programmable**: Different resolutions need different frequencies. Monitor tells us via EDID.

### Clock #4: 5× Pixel Clock

**Frequency**: Always 5× the pixel clock (165 MHz for 800×480, 742.5 MHz for 1920×1080)

**Where it goes**: RGB2DVI (HDMI serialization)

**Why 5×**: TMDS encodes 8 bits → 10 bits; need to shift 10 bits in one pixel period

**Tolerance**: Must stay locked in 5× ratio to pixel clock

**Why guaranteed**: Both from same PLL, ratio is fixed in silicon

---

## Detailed Data Flow

### Phase 1: System Startup (One Time)

When Linux boots, the display system initializes:

#### Time 0-5 ms: Drivers Load

```
Linux kernel boots
  ├─ Device tree parsed
  ├─ clk-dglnt-dynclk driver loads
  │   └─ Registers axi_dynclk_0 as a clock source
  ├─ digilent-hdmi driver loads
  │   └─ Calls probe function
  └─ DRM framework initializes
```

#### Time 5-10 ms: Read Monitor Capabilities

```
digilent_hdmi_probe() runs:
  ├─ Opens I2C connection to monitor
  ├─ Reads 128 bytes of EDID
  ├─ Parses supported resolutions
  └─ Example: "Monitor supports up to 1920×1080@60Hz"
```

#### Time 10-11 ms: Configure Clock

```
User/desktop selects resolution (e.g., 1920×1080@60Hz)
  ├─ DRM calls digilent_hdmi_atomic_mode_set()
  ├─ Extracts timing from EDID: pixel clock = 148.5 MHz
  ├─ Requests clock: clk_set_rate(hdmi_clk, 148500000)
  │
  └─ clk-dglnt-dynclk driver:
      ├─ Calculates M and D parameters for PLL
      ├─ Writes to axi_dynclk_0 registers
      ├─ PLL begins locking
      ├─ ~100 microseconds later: LOCKED signal asserts
      └─ Clock stable at 148.5 MHz
```

#### Time 11 ms: Configure VTC (Timing)

```
digilent-hdmi driver programs VTC:
  ├─ Writes H_ACTIVE = 1920 (pixels per line)
  ├─ Writes H_BLANKING = 280 (blanking period)
  ├─ Writes V_ACTIVE = 1080 (lines per frame)
  ├─ Writes V_BLANKING = 45 (blanking period)
  ├─ Writes hsync pulse width = 44 pixels
  ├─ Writes vsync pulse width = 5 lines
  ├─ Writes offsets (when sync appears in blanking)
  └─ Enables VTC
```

#### Time 11.5 ms: Configure VDMA

```
digilent-hdmi driver programs VDMA:
  ├─ Writes BASE_ADDRESS = 0x10000000 (framebuffer location)
  ├─ Writes FRAME_WIDTH = 1920 pixels
  ├─ Writes FRAME_HEIGHT = 1080 lines
  ├─ Writes STRIDE = 7680 bytes (1920 pixels × 4 bytes)
  ├─ Sets MODE = continuous (loop frame)
  └─ Enables VDMA
```

#### Time 12+ ms: Video Streaming Begins

```
Hardware is now configured and running:
  ├─ VDMA reads framebuffer from DDR
  ├─ Pixels flow through pipeline
  ├─ VTC generates timing signals
  ├─ Video Output IP holds pixels
  ├─ RGB2DVI serializes to HDMI
  └─ Monitor receives 1920×1080@60Hz video
```

### Phase 2: Continuous Streaming

Once configured, video flows continuously. Here's what happens in one frame (16.67 ms at 60 Hz):

#### Timeline: One Complete Frame

```
Time 0.00 ms: Frame starts, Line 0 begins
  VDMA reads pixels (0,0) through (1919,0)
  VTC generates timing for line 0
  Pixels appear on screen row 0
  
Time 0.11 µs per pixel: Each pixel takes 6.73 ns (at 148.5 MHz)
  VDMA: Outputs pixel (1,0)
  Converter: Receives and reformats
  Video Out: Waits for VTC gen_clken
  VTC: Counter increments, asserts gen_clken
  Video Out: Outputs to RGB2DVI
  RGB2DVI: Serializes over HDMI
  Monitor: Receives and displays pixel at (1,0)
  
Time 12.96 µs: Line 0 complete (1920 pixels)
  VDMA has read all pixels in row 0
  VTC: Counter reaches 1920, asserts hsync
  Blanking period begins (sync + porches = 280 pixels)
  No pixels sent for 40 µs (blanking time)
  
Time 13.36 µs: Line 1 begins
  (Repeat line 0 process for all 1080 lines)
  
Time 14.67 ms: Line 1079 (last active line) ends
  All 1080 lines of active video sent
  Total: 1080 × 13.36 µs = 14.43 ms
  
Time 14.67-16.67 ms: Vertical blanking
  VDMA: Completes reading line 1079, loops back to (0,0)
  VTC: Generates vertical blanking
  No pixels sent for 2.0 ms (45 line periods)
  vsync asserted (5 line periods)
  
Time 16.67 ms: Frame complete
  VDMA begins reading next frame
  VTC restarts line counter
  Monitor displays next frame
  Loop repeats at 60 Hz
```

---

## System Startup

### Complete Initialization Sequence

```
Power On (T=0 ms)
  └─ DDR initialized
  └─ ARM boots Linux kernel

Kernel loads (T=1-2 s)
  └─ File system mounts
  └─ Drivers discovered from device tree

digilent-hdmi probe (T=2-2.5 s)
  ├─ Opens I2C to monitor
  ├─ Reads EDID (128 bytes, ~10 ms)
  ├─ Parses capabilities
  └─ Registers with DRM framework

Desktop/user selects 1920×1080@60Hz (T=~5 s)
  └─ DRM calls encoder atomic_mode_set

Clock setup (T=5.010 s - T=5.011 s)
  ├─ Request: clk_set_rate(hdmi_clk, 148500000)
  ├─ PLL calculates: M=59, D=20
  ├─ Writes to registers
  ├─ PLL locks (~100 µs)
  └─ LOCKED signal asserts

VTC programming (T=5.012 s)
  ├─ Write H_ACTIVE = 1920
  ├─ Write H_BLANKING = 280
  ├─ Write V_ACTIVE = 1080
  ├─ Write V_BLANKING = 45
  ├─ Write sync parameters
  └─ Enable VTC

VDMA programming (T=5.013 s)
  ├─ Write BASE_ADDRESS = 0x10000000
  ├─ Write dimensions = 1920×1080
  ├─ Write stride = 7680 bytes
  └─ Enable VDMA

Video streaming begins (T=5.014 s)
  ├─ Pixel clock stable: 148.5 MHz
  ├─ VTC generating timing: 1920×1080@60Hz
  ├─ VDMA reading framebuffer
  ├─ Pixels flowing through pipeline
  ├─ RGB2DVI serializing to HDMI
  └─ Monitor displays video

Frame updates (T=5.014 s + onward)
  ├─ Every 16.67 ms: New frame displayed
  ├─ VDMA continuously reads framebuffer
  ├─ App writes to framebuffer (via X11/Wayland)
  ├─ Changes appear within one frame time
  └─ Continues until shutdown
```

### What Happens When Resolution Changes

```
User changes display to 1280×1024@60Hz
  ├─ DRM requests new timing from digilent-hdmi
  │
  ├─ Clock update:
  │   ├─ Calculate new frequency (108 MHz)
  │   ├─ Request: clk_set_rate(hdmi_clk, 108000000)
  │   ├─ PLL tunes (waits for LOCKED)
  │   └─ Takes ~100 microseconds
  │
  ├─ VTC update:
  │   ├─ Write new timing parameters
  │   └─ Takes ~1 millisecond
  │
  ├─ Brief video glitch during transition (< 1 frame)
  │
  └─ Display shows 1280×1024@60Hz
      (May take 10-20 ms total, unnoticeable to user)
```

---

## Summary

The PYNQ-Z2 video system is a carefully orchestrated pipeline:

1. **Memory** stores the framebuffer (picture)
2. **VDMA** reads it at high speed (200 MHz, 92 MB/s)
3. **Converter** changes format (RGBA → RGB)
4. **VTC** generates pixel/line/frame timing (from programmable pixel clock)
5. **Video Output** synchronizes pixels with timing
6. **RGB2DVI** serializes to HDMI format
7. **Monitor** receives and displays

The monitor tells the system what it needs via **EDID** (read over I2C).

The **axi_dynclk_0** generates exactly the right **pixel clock** for the chosen resolution, making the system flexible and powerful.

Understanding this architecture is essential for writing proper kernel drivers that integrate with Linux DRM.
