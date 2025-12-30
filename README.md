# 440Hz Tone Generator - Heavy Audio Engine Integration

This project demonstrates the integration of a PlugData/Heavy-generated Pure Data patch with a Raspberry Pi Pico 2 (RP2350) to output a 440Hz tone through an MCP4725 DAC.

## Features

- **Heavy Audio Engine**: Pure Data patch compiled by PlugData/Heavy for embedded systems
- **48kHz Sample Rate**: Professional audio quality
- **DMA-Based I2C**: Efficient DAC updates using DMA with minimal CPU overhead
- **Timer-Driven Processing**: Precise sample-rate timing using hardware timers
- **Ring Buffer**: Decouples audio generation from DAC output for smooth playback
- **MCP4725 DAC**: 12-bit resolution, I2C-controlled DAC with external signal conditioning for -5V to +5V output

## Architecture

### Audio Processing Flow

```
Heavy Context     Timer IRQ       Ring Buffer      DMA            MCP4725 DAC
(48kHz blocks) -> (48kHz rate) -> (256 samples) -> (I2C TX) ->  (Analog Out)
     |                |                |              |              |
  Process 32      Convert to       Buffer        Transfer       0-5V output
  samples at      12-bit DAC      samples       via DMA        (-5V to +5V
  a time          values                                       after conditioning)
```

### Key Components

1. **Heavy Audio Engine** (`440tone_c/`)
   - Pure Data patch compiled to C/C++
   - Processes audio in blocks of 32 samples
   - Zero input channels, one output channel

2. **MCP4725 DAC Driver** (`lib/dac/MCP4725.cpp`)
   - Object-oriented I2C DAC interface
   - 12-bit resolution (0-4095)
   - Fast mode I2C communication

3. **DMA Controller**
   - Handles I2C transfers asynchronously
   - Triggered by I2C TX DREQ
   - IRQ handler chains transfers from ring buffer

4. **Ring Buffer**
   - 256 samples capacity (power of 2 for efficiency)
   - Thread-safe write/read operations
   - Tracks buffer underruns

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
Sample Rate: 48000 Hz
Buffer Size: 32 samples

Initializing MCP4725 DAC...
MCP4725: Current DAC=0, EEPROM=0, PowerDown=0
MCP4725: Initialized successfully at address 0x60

Initializing DMA...
DMA initialized: channel=0

Initializing Heavy audio engine...
Heavy context created:
  Sample rate: 48000 Hz
  Input channels: 0
  Output channels: 1
  Context size: XXXX bytes

Starting audio processing...
Audio engine running!
Playing 440Hz tone...

Status: Samples=240000, Underruns=0, Buffer=45/256
```

## Performance

- **Sample Rate**: 48,000 Hz
- **Timer Interval**: ~20.83 μs per sample
- **Processing Block**: 32 samples every 666.67 μs
- **DMA Transfer**: ~60 μs per I2C write (400kHz I2C)
- **CPU Usage**: ~2-3% (most work done by DMA)

## Audio Signal Path

1. **Heavy Processing**: Generates float samples [-1.0, +1.0]
2. **Conversion**: Maps to 12-bit DAC values [0, 4095]
   - -1.0 → 0 (0V DAC → -5V output)
   - 0.0 → 2048 (2.5V DAC → 0V output)
   - +1.0 → 4095 (5V DAC → +5V output)
3. **Ring Buffer**: Stores samples awaiting transmission
4. **DMA Transfer**: Writes to MCP4725 via I2C
5. **External Conditioning**: Scales 0-5V to -5V to +5V

## Customization

### Change Sample Rate
Edit `SAMPLE_RATE` in `test_440.cpp`:
```cpp
#define SAMPLE_RATE 44100.0f  // Or any other rate
```

### Adjust Buffer Sizes
```cpp
#define BUFFER_SIZE 32         // Heavy processing block size
#define RING_BUFFER_SIZE 256   // Ring buffer (must be power of 2)
```

### Use Different PlugData Patch
1. Export your patch from PlugData using Heavy
2. Replace contents of `440tone_c/` folder
3. Update `Heavy_440tone.h` include if patch name differs
4. Update CMakeLists.txt if new source files are added

## Troubleshooting

### No Audio Output
- Check I2C connections (GPIO2/SDA, GPIO3/SCL)
- Verify DAC address (use I2C scanner)
- Check external signal conditioning circuit
- Monitor UART output for initialization errors

### Buffer Underruns
- Increase `RING_BUFFER_SIZE`
- Reduce I2C traffic from other peripherals
- Verify timer interrupt priority

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
