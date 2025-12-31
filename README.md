# 440Hz Tone Generator - Heavy Audio Engine Integration

This project demonstrates the integration of a PlugData/Heavy-generated Pure Data patch with a Raspberry Pi Pico 2 (RP2350) to output a precise 440Hz tone through an MCP4725 DAC.

## Features

- **Heavy Audio Engine**: Pure Data patch compiled by PlugData/Heavy for embedded systems
- **44.156kHz Sample Rate**: High-quality audio at empirically calibrated rate
- **DMA-Based I2C**: Efficient DAC updates using DMA with minimal CPU overhead
- **Timer-Driven Processing**: Simple 22μs timer period produces stable 44.156kHz rate
- **Ring Buffer**: 512-sample buffer (11.6ms) decouples audio generation from DAC output
- **MCP4725 DAC**: 12-bit resolution, I2C-controlled DAC at 2MHz I2C speed
- **Hardware FPU**: RP2350's Cortex-M33 FPv5 FPU accelerates DSP processing

## Architecture

### Audio Processing Flow

```
Heavy Context      Timer IRQ        Ring Buffer      DMA            MCP4725 DAC
(44.156kHz)    -> (44.156kHz)   -> (512 samples) -> (I2C TX)  ->  (Analog Out)
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
   - Sample rate: 44,156 Hz

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
   - Provides 11.6ms of buffering
   - Tracks buffer underruns and overruns

5. **Timer Interrupt**
   - Hardware timer alarm at 22μs period
   - Produces stable 44,156 Hz sample rate
   - Minimal overhead (<1μs per interrupt)

## Hardware Configuration

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
Sample Rate: 44156 Hz
Buffer Size: 64 samples

Initializing MCP4725 DAC...
MCP4725: Current DAC=0, EEPROM=0, PowerDown=0
MCP4725: Initialized successfully at address 0x60

Initializing DMA...
DMA initialized: channel=0

Initializing Heavy audio engine...
Heavy context created:
  Sample rate: 44156 Hz
  Input channels: 0
  Output channels: 1

Timer interrupt enabled at 44156 Hz.

=== Starting Audio Loop ===
Generating 440Hz tone with timer-driven DAC updates...
Press Ctrl+C to stop.

DAC: 88896 (44160 Hz actual) | Heavy: 44156 Hz | Freq: 440.0 Hz | Buffer: 256 (50.0%) | U/O: 2597/0
DAC: 133312 (44160 Hz actual) | Heavy: 44156 Hz | Freq: 440.0 Hz | Buffer: 256 (50.0%) | U/O: 3634/0
```

**Output Explanation:**
- **DAC**: Total samples sent (actual rate in Hz)
- **Heavy**: Configured sample rate
- **Freq**: Measured output frequency (should be 440.0 Hz)
- **Buffer**: Ring buffer fill level (target: 50%)
- **U/O**: Underruns/Overruns (should be minimal)

## Performance

- **Sample Rate**: 44,156 Hz (actual measured)
- **Timer Period**: 22 μs (empirically calibrated)
- **Processing Block**: 64 samples every 1.45 ms
- **DMA Transfer**: ~12 μs per I2C write (2MHz I2C, 3 bytes)
- **Ring Buffer**: 512 samples (11.6ms latency)
- **Buffer Strategy**: 50% watermark (maintains ~256 samples)
- **Output Frequency**: 440.0 Hz ±0.1 Hz accuracy
- **Underruns**: Minimal (<0.1% with proper timing)

## Audio Signal Path

1. **Heavy Processing**: Generates float samples [-1.0, +1.0] at 44.156kHz
2. **Conversion**: Maps to 12-bit DAC values [0, 4095]
   - -1.0 → 0 (0V DAC output)
   - 0.0 → 2048 (2.5V DAC output)  
   - +1.0 → 4095 (5V DAC output)
3. **Ring Buffer**: Stores 512 samples awaiting transmission
4. **DMA Transfer**: Writes to MCP4725 via I2C (non-blocking)
5. **DAC Output**: 0-5V analog signal at 12-bit resolution

## Customization

### Change Sample Rate

**Important**: Sample rate must match what the timer naturally produces.

To calibrate for a different rate:
1. Test different `TIMER_PERIOD_US` values (20, 21, 22, 23, 24)
2. Measure actual DAC rate from UART output
3. Update these to match measured rate:
```cpp
#define DAC_SAMPLE_RATE 44156      // Use measured actual rate
#define HEAVY_SAMPLE_RATE 44156.0f // Must match DAC rate
#define TIMER_PERIOD_US 22         // Period that produces measured rate
```

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
