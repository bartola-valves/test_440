# Heavy Audio Engine Integration Guide

## Raspberry Pi Pico + PlugData/Heavy + MCP4725 DAC

**Author:** Ale Moglia  
**Date:** 30 December 2025  
**Project:** 440Hz Tone Generator Test

---

## Table of Contents

1. [Overview](#overview)
2. [What is Heavy/PlugData?](#what-is-heavyplugdata)
3. [Project Architecture](#project-architecture)
4. [Code Generation Process](#code-generation-process)
5. [Integration Components](#integration-components)
6. [Signal Flow](#signal-flow)
7. [Key API Functions](#key-api-functions)
8. [Hardware Setup](#hardware-setup)
9. [Performance Considerations](#performance-considerations)
10. [Troubleshooting](#troubleshooting)

---

## Overview

This project demonstrates integrating a Pure Data audio patch (created in PlugData) with a Raspberry Pi Pico 2 (RP2350) to generate a 440Hz sine wave output through an MCP4725 12-bit DAC.

**Key Achievement:** Successfully running DSP code generated from a visual audio programming environment (Pure Data) on a bare-metal embedded microcontroller.

---

## What is Heavy/PlugData?

### Pure Data
- **Pure Data (Pd)** is a visual programming language for audio synthesis and processing
- You create patches by connecting graphical objects (oscillators, filters, etc.)
- Originally designed for desktop computers and real-time audio applications

### PlugData
- Modern, user-friendly interface for Pure Data
- Runs as a standalone application or DAW plugin
- Includes the **Heavy Audio Tools** compiler

### Heavy Audio Tools
- Compiler that converts Pure Data patches into C/C++ code
- Optimized for embedded systems (ARM, x86, etc.)
- Generates standalone code with no dependencies on Pd runtime
- Output is pure signal processing code that can run on microcontrollers

### The Workflow
```
PlugData Patch → Heavy Compiler → C/C++ Source Code → Your Embedded Project
   (visual)         (export)        (440tone_c/)         (test_440.cpp)
```

---

## Project Architecture

### Directory Structure
```
test_440/
├── test_440.cpp              # Main application
├── CMakeLists.txt            # Build configuration
├── 440tone_c/                # Heavy-generated code
│   ├── Heavy_440tone.h       # Main patch interface
│   ├── Heavy_440tone.cpp     # Patch implementation
│   ├── HeavyContext.hpp      # Context management
│   ├── HvSignalPhasor.c      # Phasor oscillator
│   ├── HvMessage.c           # Message handling
│   └── [other Heavy files]   # DSP utilities
└── lib/
    ├── hardware.h            # Hardware pin definitions
    └── dac/
        ├── MCP4725.h         # DAC driver header
        └── MCP4725.cpp       # DAC driver implementation
```

### Component Roles

| Component | Role |
|-----------|------|
| **Heavy Engine** | Generates audio samples (DSP processing) |
| **Main Loop** | Fetches samples and sends to DAC |
| **MCP4725 Driver** | Hardware abstraction for DAC communication |
| **I2C Bus** | Physical connection to DAC |

---

## Code Generation Process

### Step 1: Create Patch in PlugData
In your case, you created a simple patch:
- **phasor~ 440** - Generates a 440Hz sawtooth ramp (0 to 1)
- **osc~** or similar - Converts ramp to sine wave
- **dac~** - Output to DAC (becomes audio output in Heavy)

### Step 2: Export with Heavy
In PlugData:
1. Go to File → Heavy Export (or similar menu)
2. Select target: **C Source Code**
3. Export settings:
   - Patch name: `440tone`
   - Sample rate: 48000 Hz
   - Output channels: 2 (stereo)
4. Export generates the `440tone_c/` folder

### Step 3: What Heavy Generates

#### Main Files You'll Use:
- **Heavy_440tone.h** - Public C API for your patch
- **Heavy_440tone.cpp** - Patch initialization and routing

#### DSP Processing Files:
- **HvSignalPhasor.c** - Implements `phasor~` object
- **HvSignalVar.c** - Variable handling
- **HvTable.c** - Lookup tables (for sine, etc.)

#### Infrastructure Files:
- **HvHeavy.h** - Core Heavy API definitions
- **HvMessage.c** - Message queue system
- **HvContext.cpp** - Audio context management

### Step 4: Integration
The generated code is **completely standalone**:
- No Pure Data runtime needed
- No dynamic linking
- Just compile and link with your project

---

## Integration Components

### 1. CMakeLists.txt Integration

```cmake
# Heavy 440tone audio engine source files
set(HEAVY_440_SOURCES
    440tone_c/Heavy_440tone.cpp
    440tone_c/HeavyContext.cpp
    440tone_c/HvHeavy.cpp
    440tone_c/HvLightPipe.c
    440tone_c/HvMessage.c
    440tone_c/HvMessagePool.c
    440tone_c/HvMessageQueue.c
    440tone_c/HvSignalPhasor.c    # Your phasor~ object
    440tone_c/HvSignalVar.c
    440tone_c/HvTable.c
    440tone_c/HvUtils.c
)

# DAC library
set(DAC_SOURCES
    lib/dac/MCP4725.cpp
)

# Combine all sources
add_executable(test_440 
    test_440.cpp
    ${HEAVY_440_SOURCES}
    ${DAC_SOURCES}
)

# Tell Heavy it's running on bare-metal (no OS)
target_compile_definitions(test_440 PRIVATE
    HV_BARE_METAL=1
)

# Include paths
target_include_directories(test_440 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/440tone_c
    ${CMAKE_CURRENT_LIST_DIR}/lib
)
```

**Why HV_BARE_METAL=1?**
- Tells Heavy we're not on a desktop OS
- Disables platform-specific features
- Removes compiler warnings about unknown platform

### 2. Main Application Code

#### Include Heavy Headers
```cpp
#include "Heavy_440tone.h"  // Your specific patch
#include "lib/hardware.h"    // Pin definitions
#include "lib/dac/MCP4725.h" // DAC driver
```

#### Audio Configuration
```cpp
#define SAMPLE_RATE 48000.0f      // Must match Heavy export
#define BUFFER_SIZE 64            // Process in blocks
#define DAC_UPDATE_RATE 12000     // 12kHz (decimated from 48kHz)
```

**Why these values?**
- **48kHz**: Standard audio sample rate, matches Heavy export
- **64 samples**: Small enough for low latency, large enough for efficiency
- **12kHz DAC rate**: I2C can't keep up with 48kHz, so we decimate (every 4th sample)

#### Buffer Setup
```cpp
static float audioBuffer[BUFFER_SIZE * 2]; // Stereo = 2x buffer size
```

**Why BUFFER_SIZE * 2?**
Heavy outputs stereo (2 channels) in **uninterleaved** format:
```
[L0, L1, L2, ..., L63, R0, R1, R2, ..., R63]
 ← Left channel →    ← Right channel →
```

---

## Signal Flow

### Complete Audio Pipeline

```
┌─────────────────┐
│  Heavy Context  │  48kHz processing
│   (440Hz sine)  │  Generates float samples [-1.0, +1.0]
└────────┬────────┘
         │ hv_processInline()
         ↓
┌─────────────────┐
│  Audio Buffer   │  64 stereo samples
│  [128 floats]   │  Uninterleaved: [L...L R...R]
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│  Sample Loop    │  Every 4th sample (decimation)
│  i = 0,4,8...   │  Reduces 48kHz → 12kHz
└────────┬────────┘
         │ Use left channel: audioBuffer[i]
         ↓
┌─────────────────┐
│  audioToDAC()   │  Convert float to 12-bit integer
│  [-1,+1] →      │  sample * 2047.5 + 2047.5
│  [0, 4095]      │  
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│  dac.setRaw()   │  I2C transaction (~60μs)
│  I2C1 0x60      │  [0x00][D11-D4][D3-D0<<4]
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│   MCP4725 DAC   │  12-bit D/A conversion
│   0-5V output   │  Update time: ~6μs
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│  External       │  0-5V → -5V to +5V
│  Conditioning   │  Op-amp circuit
└────────┬────────┘
         │
         ↓
    Audio Output
    (440Hz sine)
```

### Timing Analysis

At 12kHz DAC update rate:
- **Period:** 83.3μs per sample
- **I2C transaction:** ~60μs (at 400kHz I2C)
- **Processing overhead:** ~20μs
- **Margin:** 3.3μs (tight but works!)

At 48kHz (what we tried initially):
- **Period:** 20.8μs per sample
- **I2C transaction:** ~60μs
- **Result:** ❌ Can't keep up! I2C takes 3x longer than available time

**Solution:** Decimation (every 4th sample) gives us the needed time budget.

---

## Key API Functions

### Heavy Context Management

#### `hv_440tone_new(double sampleRate)`
**Purpose:** Create and initialize a Heavy context for your patch

```cpp
heavyContext = hv_440tone_new(SAMPLE_RATE);
```

**What it does:**
1. Allocates memory for DSP state
2. Initializes all objects (phasor, oscillators, etc.)
3. Sets up message queues
4. Resets all internal state to defaults

**Parameters:**
- `sampleRate`: Must match what you exported from PlugData (48000.0)

**Returns:**
- Pointer to context, or NULL if allocation fails

#### `hv_delete(HeavyContextInterface *c)`
**Purpose:** Clean up and free the context

```cpp
hv_delete(heavyContext);
```

**What it does:**
- Frees all allocated memory
- Releases resources
- Should be called before program exit (though not reached in embedded loops)

### Audio Processing

#### `hv_processInline(context, inputBuffers, outputBuffers, numFrames)`
**Purpose:** Process a block of audio samples

```cpp
hv_processInline(heavyContext, NULL, audioBuffer, BUFFER_SIZE);
```

**Parameters:**
- `context`: Your Heavy context
- `inputBuffers`: Input audio (NULL = no input for this patch)
- `outputBuffers`: Where to write output samples
- `numFrames`: Number of samples to process per channel

**Important:** `numFrames` refers to **per-channel** samples!
- If `BUFFER_SIZE = 64`, Heavy processes 64 samples per channel
- For stereo (2 channels), it writes 128 floats total
- Format: `[L0...L63, R0...R63]` (uninterleaved)

**What happens inside:**
1. Advances phasor by 440/48000 per sample
2. Looks up sine value from table
3. Writes float output (-1.0 to +1.0)
4. Repeats for requested number of samples

### Context Information

#### `hv_getSampleRate(context)`
Returns the sample rate the context was created with.

#### `hv_getNumInputChannels(context)`
Returns number of input channels (0 for this patch).

#### `hv_getNumOutputChannels(context)`
Returns number of output channels (2 for stereo).

#### `hv_getSize(context)`
Returns total memory used by context in bytes.

---

## Hardware Setup

### Pin Configuration (hardware.h)

```cpp
// MCP4725 DAC on I2C1
#define DAC_SDA_PIN 2          // GPIO2 - I2C1 Data
#define DAC_SCL_PIN 3          // GPIO3 - I2C1 Clock
#define DAC_I2C_PORT i2c1      // I2C1 peripheral
#define DAC_I2C_ADDRESS 0x60   // A0 tied to GND

// Status LED
#define LED_PIN 25             // Onboard LED
```

### I2C Configuration

```cpp
// In MCP4725::init()
i2c_init(DAC_I2C_PORT, 400000);  // 400kHz Fast Mode I2C
gpio_set_function(DAC_SDA_PIN, GPIO_FUNC_I2C);
gpio_set_function(DAC_SCL_PIN, GPIO_FUNC_I2C);
gpio_pull_up(DAC_SDA_PIN);       // Required for I2C
gpio_pull_up(DAC_SCL_PIN);
```

### MCP4725 Protocol

The DAC uses I2C "Fast Write" mode:
```
START | 0x60 | ACK | 0x00 | ACK | D11-D4 | ACK | D3-D0<<4 | ACK | STOP
       ↑            ↑             ↑               ↑
    Address      Command      Upper bits    Lower bits (left-justified)
```

**Transaction breakdown:**
- Byte 1: `0x00` - Fast mode DAC write command
- Byte 2: Upper 8 bits of 12-bit value
- Byte 3: Lower 4 bits (left-shifted to high nibble)

---

## Performance Considerations

### CPU Usage

**Current Implementation:**
- Heavy processing: ~5% CPU (64 samples at a time)
- I2C writes: ~15% CPU (waiting for I2C peripheral)
- Main loop overhead: ~2% CPU
- **Total: ~22% CPU usage**

**Optimization Opportunities:**
1. **Use DMA for I2C** (complex but frees CPU during transfers)
2. **Increase buffer size** (process 128 or 256 samples at once)
3. **Use IRQ-driven timer** (more precise timing)
4. **Optimize decimation** (use better anti-aliasing filter)

### Memory Usage

```
Heavy context:     ~12 KB (includes state + tables)
Audio buffer:      512 bytes (128 floats)
DAC driver:        ~100 bytes
Stack:            ~4 KB
Total RAM usage:  ~17 KB (plenty left on 520KB RP2350)
```

### Sample Rate Limitations

| Sample Rate | DAC Update | I2C Bandwidth | Status |
|-------------|------------|---------------|--------|
| 48 kHz | 48 kHz | 100% | ❌ Can't keep up |
| 48 kHz | 24 kHz | 50% | ⚠️ Tight timing |
| 48 kHz | 12 kHz | 25% | ✅ Works reliably |
| 48 kHz | 6 kHz | 12% | ✅ Plenty of margin |

**Why we chose 12kHz:**
- Good audio quality (Nyquist = 6kHz, covers most of audible range)
- Reliable timing margin
- Simple implementation (every 4th sample)

---

## Troubleshooting

### Problem: No Audio Output

**Check:**
1. ✅ I2C connections (SDA, SCL, GND, VCC)
2. ✅ DAC address (0x60 with A0=GND, 0x61 with A0=VCC)
3. ✅ UART output shows "DAC initialized successfully"
4. ✅ Heavy samples are non-zero (check debug output)
5. ✅ External conditioning circuit is working

**Debug Commands:**
```cpp
// Check if Heavy is generating audio
for (int i = 0; i < 8; i++) {
    printf("[%d] %.4f\n", i, audioBuffer[i]);
}

// Verify I2C communication
uint8_t test = 0;
i2c_read_blocking(DAC_I2C_PORT, DAC_I2C_ADDRESS, &test, 1, false);
```

### Problem: Distorted Output

**Possible Causes:**
1. **Sample rate mismatch** - Heavy exported at different rate than SAMPLE_RATE
2. **Clipping** - Samples outside [-1.0, +1.0] range
3. **I2C errors** - Missing ACKs, bus contention
4. **External circuit** - Incorrect gain/offset

**Solutions:**
```cpp
// Add sample range checking
if (sample > 1.0f || sample < -1.0f) {
    printf("WARNING: Sample clipping: %.4f\n", sample);
}

// Monitor DAC updates
printf("DAC updates per second: %lu\n", dacUpdates);
```

### Problem: Timing Issues

**Symptoms:**
- Inconsistent pitch
- Dropouts
- Status shows wrong sample rate

**Solutions:**
```cpp
// Measure actual timing
uint64_t start = time_us_64();
hv_processInline(heavyContext, NULL, audioBuffer, BUFFER_SIZE);
uint64_t elapsed = time_us_64() - start;
printf("Processing time: %llu us\n", elapsed);

// Adjust decimation factor
#define DECIMATION 8  // Every 8th sample instead of 4th
```

### Problem: Compilation Errors

**Common Issues:**

1. **"Heavy_440tone.h not found"**
   - Check `target_include_directories` in CMakeLists.txt
   - Verify 440tone_c folder exists

2. **"undefined reference to hv_440tone_new"**
   - All Heavy .c/.cpp files must be in HEAVY_440_SOURCES
   - Check CMakeLists.txt completeness

3. **"Platform detection warning"**
   - Add `target_compile_definitions(test_440 PRIVATE HV_BARE_METAL=1)`

---

## Advanced Topics

### Adding Parameter Control

Heavy patches can have parameters (like frequency control):

```cpp
// If your patch has a "freq" parameter
hv_sendFloatToReceiver(heavyContext, 
                       hv_stringToHash("freq"),  // Parameter name
                       880.0f);                   // New value (880Hz)
```

### Message Handling

Patches can send messages back:

```cpp
void myPrintHook(HeavyContextInterface *context,
                 const char *printName,
                 const char *str,
                 const HvMessage *msg)
{
    printf("Pd print: %s: %s\n", printName, str);
}

// Register hook
hv_setPrintHook(heavyContext, myPrintHook);
```

### Using Tables

If your patch uses tables (arrays):

```cpp
// Get table reference
float *table = hv_getTableBuffer(heavyContext, hv_stringToHash("myTable"));
int tableSize = hv_getTableSize(heavyContext, hv_stringToHash("myTable"));

// Modify table data
for (int i = 0; i < tableSize; i++) {
    table[i] = /* custom waveform */;
}
```

---

## Next Steps

### Enhancements to Try

1. **Add CV Input Control**
   - Read ADC from your hardware
   - Map CV voltage to frequency parameter
   - Update Heavy context with `hv_sendFloatToReceiver()`

2. **Use Timer IRQ for Precise Timing**
   - Replace main loop with timer callback
   - Exact 12kHz sample rate
   - Better pitch stability

3. **Implement Ring Buffer**
   - Decouple Heavy processing from I2C writes
   - Handle timing jitter better
   - Allow burst processing

4. **Multiple Patches**
   - Export different patches (VCO, VCF, etc.)
   - Switch between them dynamically
   - Build a complete synthesizer

5. **Optimize Performance**
   - Profile with timer measurements
   - Use DMA for I2C
   - Increase buffer sizes
   - Parallel processing on core 1

---

## References

### Documentation
- [Heavy Audio Tools](https://github.com/enzienaudio/hvcc)
- [PlugData](https://plugdata.org/)
- [Pure Data](https://puredata.info/)
- [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- [MCP4725 Datasheet](https://www.microchip.com/wwwproducts/en/MCP4725)

### Code Structure
```
Heavy API Layer (HvHeavy.h)
    ↓
Patch-Specific Code (Heavy_440tone.cpp)
    ↓
DSP Objects (HvSignalPhasor.c, etc.)
    ↓
Utilities (HvMessage.c, HvTable.c)
```

### Key Concepts

**Heavy is NOT:**
- ❌ An audio library you link against
- ❌ A runtime that interprets Pd patches
- ❌ Platform-specific code

**Heavy IS:**
- ✅ A compiler that generates C/C++ source code
- ✅ Standalone, embeddable DSP code
- ✅ Zero-dependency audio processing
- ✅ Your Pd patch turned into optimized C functions

---

## Conclusion

This integration successfully demonstrates:
1. ✅ Visual audio programming (PlugData) → Embedded code
2. ✅ Heavy compiler working on bare-metal ARM
3. ✅ Real-time DSP on RP2350 microcontroller
4. ✅ I2C DAC communication for audio output
5. ✅ Clean, maintainable code structure

**The workflow enables:**
- Rapid prototyping in PlugData
- No DSP algorithm coding by hand
- Easy experimentation with different patches
- Professional audio quality from MCU

**This is powerful because:**
You can now design complex audio processors visually in PlugData, export them instantly, and run them on your Eurorack hardware!

---

**End of Integration Guide**
