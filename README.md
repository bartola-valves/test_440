# 440Hz Tone Generator - Heavy Audio Engine Integration

This project demonstrates the integration of a PlugData/Heavy-generated Pure Data patch with a Raspberry Pi Pico 2 (RP2350) to output a precise 440Hz tone through an MCP4725 DAC.

## Features

- **Heavy Audio Engine**: Pure Data patch compiled by PlugData/Heavy for embedded systems
- **40kHz Sample Rate**: Clean rate with exact 25μs timer period
- **DMA-Based I2C**: Efficient DAC updates using DMA with minimal CPU overhead
- **Timer-Driven Processing**: Simple 25μs timer period produces exact 40kHz rate
- **Ring Buffer**: 512-sample buffer (12.8ms) decouples audio generation from DAC output
- **MCP4725 DAC**: 12-bit resolution, I2C-controlled DAC at 2MHz I2C speed
- **Hardware FPU**: RP2350's Cortex-M33 FPv5 FPU accelerates DSP processing
- **Verified Output**: 440.0 Hz sine wave measured on oscilloscope

## Architecture

### Audio Processing Flow

```
Heavy Context      Timer IRQ        Ring Buffer      DMA            MCP4725 DAC
(40kHz)        -> (40kHz)       -> (512 samples) -> (I2C TX)  ->  (Analog Out)
     |                 |                |              |              |
  Process 64       Convert to       Buffer         Transfer       0-5V output
  samples at       12-bit DAC      samples        via DMA        (12-bit res)
  a time           values          (50% full)     (2MHz I2C)     
```

### Key Components

1. **Heavy Audio Engine** (`440tone_c/`)
   - Pure Data patch compiled to C/C++
   - Processes audio in blocks of 64 samples
   - Zero input channels, one output channel (mono)
   - Sample rate: 40,000 Hz

2. **MCP4725 DAC Driver** (`lib/dac/MCP4725.cpp`)
   - Object-oriented I2C DAC interface
   - 12-bit resolution (0-4095)
   - 2MHz I2C speed for fast updates

3. **DMA Controller**
   - Handles I2C transfers asynchronously
   - Triggered by I2C TX DREQ (hardware paced)
   - Non-blocking transfers (~12μs per sample)

4. **Ring Buffer**
   - 512 samples capacity (power of 2 for efficiency)
   - 50% watermark strategy (256 samples)
   - Provides 12.8ms of buffering at 40kHz
   - Tracks buffer underruns and overruns

5. **Timer Interrupt**
   - Hardware timer alarm at 25μs period (exact)
   - Produces stable 40,000 Hz sample rate
   - 420ns overhead (1.7% CPU usage)

## Hardware Configuration

I'm reusing my "Joker2" hardware platform for testing here:

As defined in `lib/hardware.h`:

- **DAC I2C**: I2C1, GPIO2 (SDA), GPIO3 (SCL)
- **DAC Address**: 0x60 (A0 tied to GND)
- **Status LED**: GPIO25 (onboard LED)
- **UART Debug**: GPIO0 (TX), GPIO1 (RX), 115200 baud

## Building and Running

### Build
```bash
# The project uses CMake and Ninja
ninja -C build
```

Or use the VS Code task: "Compile Project"

### Flash to Pico
```bash
# Using picotool (USB boot mode)
picotool load build/test_440.uf2 -fx

# Or using OpenOCD (debug probe)
# Use VS Code task: "Flash"
```

### Monitor Output
```bash
# Connect to UART at 115200 baud
minicom -D /dev/ttyUSB0 -b 115200
```

Expected output:
```
=== Heavy 440Hz Tone Generator ===
Sample Rate: 40000 Hz
Buffer Size: 64 samples

Initializing MCP4725 DAC...
MCP4725: Current DAC=0, EEPROM=0, PowerDown=0
MCP4725: Initialized successfully at address 0x60

Initializing DMA...
DMA initialized: channel=0

Initializing Heavy audio engine...
Heavy context created:
  Sample rate: 40000 Hz
  Input channels: 0
  Output channels: 1

Timer interrupt enabled at 40000 Hz.

=== Starting Audio Loop ===
Generating 440Hz tone with timer-driven DAC updates...
Press Ctrl+C to stop.

DAC: 80000 (40000 Hz actual) | Heavy: 40000 Hz | Freq: 440.0 Hz | Buffer: 256 (50.0%) | U/O: 0/0
DAC: 120000 (40000 Hz actual) | Heavy: 40000 Hz | Freq: 440.0 Hz | Buffer: 256 (50.0%) | U/O: 0/0
```

**Output Explanation:**
- **DAC**: Total samples sent (actual rate in Hz)
- **Heavy**: Configured sample rate
- **Freq**: Measured output frequency (should be 440.0 Hz)
- **Buffer**: Ring buffer fill level (target: 50%)
- **U/O**: Underruns/Overruns (should be minimal)

## Performance

- **Sample Rate**: 40,000 Hz (exact with 25μs timer period)
- **Timer Period**: 25 μs (verified on oscilloscope)
- **Interrupt Overhead**: 420 ns (measured on GPIO24 test pin)
- **CPU Usage**: 1.7% for timer interrupts (420ns / 25μs)
- **Processing Block**: 64 samples every 1.6 ms
- **DMA Transfer**: ~12 μs per I2C write (2MHz I2C, 3 bytes)
- **Ring Buffer**: 512 samples (12.8ms latency)
- **Buffer Strategy**: 50% watermark (maintains ~256 samples)
- **Output Frequency**: 440.0 Hz (verified on oscilloscope)
- **Underruns**: Zero with proper timing
- **CPU Headroom**: ~98% available for Heavy DSP processing

### Measured Results
- Timer period: **25.0 μs** (oscilloscope verified)
- Output frequency: **440.0 Hz** (oscilloscope verified)
- System: **Stable and accurate**

## Audio Signal Path

1. **Heavy Processing**: Generates float samples [-1.0, +1.0] at 40kHz
2. **Conversion**: Maps to 12-bit DAC values [0, 4095]
   - -1.0 → 0 (0V DAC output)
   - 0.0 → 2048 (2.5V DAC output)  
   - +1.0 → 4095 (5V DAC output)
3. **Ring Buffer**: Stores 512 samples awaiting transmission (12.8ms @ 40kHz)
4. **DMA Transfer**: Writes to MCP4725 via I2C (non-blocking)
5. **DAC Output**: 0-5V analog signal at 12-bit resolution
6. **Result**: Perfect 440Hz sine wave

## Customization

### Change Sample Rate

**Best Practice**: Choose a sample rate with an exact integer timer period.

**Recommended rates:**
- **40,000 Hz** → 25 μs (exact, verified)
- **50,000 Hz** → 20 μs (exact)
- **32,000 Hz** → 31.25 μs (requires fractional)
- **48,000 Hz** → 20.833 μs (requires fractional)

```cpp
#define DAC_SAMPLE_RATE 40000      // Sample rate in Hz
#define HEAVY_SAMPLE_RATE 40000.0f // Must match DAC rate
#define TIMER_PERIOD_US 25         // 1,000,000 / 40,000 = 25μs
```

**Important**: Regenerate Heavy patch with matching sample rate!

### Adjust Buffer Sizes
```cpp
#define BUFFER_SIZE 64             // Heavy processing block size
#define RING_BUFFER_SIZE 512       // Ring buffer (must be power of 2)
#define BUFFER_LOW_WATERMARK 256   // When to refill (50% of ring buffer)
```

### Use Different PlugData Patch
1. Export your patch from PlugData using Heavy Audio Tools
2. Set output sample rate in Heavy to match your measured rate (44156 Hz)
3. Replace contents of `440tone_c/` folder
4. Update `Heavy_440tone.h` include if patch name differs
5. Rebuild project

## Troubleshooting

### No Audio Output
- Check I2C connections (GPIO2/SDA, GPIO3/SCL)
- Verify DAC address with I2C scanner (should be 0x60)
- Monitor UART output for initialization errors
- Check MCP4725 power supply (3.3V or 5V)

### Wrong Frequency Output
- Verify DAC_SAMPLE_RATE matches actual measured rate
- Ensure HEAVY_SAMPLE_RATE matches DAC_SAMPLE_RATE exactly
- Regenerate Heavy patch with correct sample rate
- Check timer period produces stable rate

### Buffer Underruns
- Increase `RING_BUFFER_SIZE` (must be power of 2)
- Lower `BUFFER_LOW_WATERMARK` threshold
- Reduce processing block size in Heavy patch
- Check for I2C bus contention

### Distorted Output
- Check sample rate matches patch export settings
- Verify external conditioning circuit gain
- Check for I2C communication errors

## License

- **Heavy Engine**: Copyright Enzien Audio, Ltd. (See source files for license)
- **Application Code**: Copyright 2025 Bartola Ltd. UK / Alejandro Moglia
- **MCP4725 Driver**: Copyright 2025 Alejandro Moglia

## References

- [Heavy Audio Tools](https://www.enzienaudio.com/)
- [PlugData](https://plugdata.org/)
- [MCP4725 Datasheet](https://www.microchip.com/wwwproducts/en/MCP4725)
- [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
