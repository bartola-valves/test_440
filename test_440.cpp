/**
 * @file test_440.cpp
 * @brief 440Hz Tone Generator - DMA-Driven I2C DAC Updates
 * @author Ale Moglia
 * @date 31 December 2025
 *
 * ARCHITECTURE OVERVIEW:
 * ======================
 * This implementation achieves 44.156kHz sample rate using DMA for non-blocking I2C transfers.
 * Sample rate is derived from empirical measurement of timer performance with 22μs period.
 *
 * SIGNAL PATH:
 * Heavy DSP Engine (44.156kHz) → Ring Buffer → Timer Interrupt (44.156kHz) → DMA → I2C → MCP4725 DAC
 *
 * KEY INNOVATIONS:
 * - DMA handles I2C transfers asynchronously (~12μs per transfer at 2MHz I2C)
 * - Timer interrupt only queues DMA transfers (<1μs overhead)
 * - Ring buffer decouples audio generation from DAC updates
 * - Achieves perfect 440Hz sine wave at 44.156kHz sample rate
 * - Simple timer implementation: 22μs period naturally produces 44.156kHz
 *
 * TIMING BUDGET (per sample @ 44.156kHz = 22.65μs):
 * - Timer interrupt: ~0.5μs (check DMA, queue transfer, advance buffer)
 * - DMA transfer: ~12μs (handled by hardware in background)
 * - Heavy processing: Amortized across 64-sample blocks
 * - Ring buffer: 512 samples (11.6ms @ 44.156kHz) provides elasticity
 *
 * SAMPLE RATE CALIBRATION:
 * The timer period of 22μs was empirically determined to produce 44.156kHz actual rate.
 * Heavy DSP engine is configured to match this exact rate for correct 440Hz output.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "pico/platform.h" // For time functions// needed for RP2350 FPU access

#include "Heavy_440tone.h"
#include "lib/hardware.h"
#include "lib/dac/MCP4725.h"

// Audio configuration
// Audio configuration - Use ACTUAL measured timer rate for perfect accuracy
#define DAC_SAMPLE_RATE 44156      // Actual measured rate with 22μs period
#define HEAVY_SAMPLE_RATE 44156.0f // MUST match actual DAC rate for correct 440Hz
#define BUFFER_SIZE 64             // Heavy processing block size

// Timer period that produces the measured rate
#define TIMER_PERIOD_US 22 // Measured: produces 44,156 Hz actual

// Ring buffer configuration
#define RING_BUFFER_SIZE 512 // Power of 2 for efficient modulo
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)
#define BUFFER_LOW_WATERMARK (RING_BUFFER_SIZE / 2) // Keep buffer at least 50% full (256 samples = 5.8ms)

// Ring buffer for DAC samples (16-bit values ready for DAC)
static volatile uint16_t ringBuffer[RING_BUFFER_SIZE];
static volatile uint32_t writeIndex = 0; // Where Heavy writes
static volatile uint32_t readIndex = 0;  // Where IRQ reads

// Audio buffers
static float audioBuffer[BUFFER_SIZE * 2]; // Stereo output from Heavy

// Heavy context
static HeavyContextInterface *heavyContext = NULL;

// MCP4725 DAC instance
static MCP4725 dac;

// DMA channel for I2C transfers
static int dma_chan = -1;
static uint16_t dma_i2c_buffer[3]; // 16-bit buffer for I2C data_cmd writes

// Statistics
static volatile uint32_t samplesGenerated = 0;
static volatile uint32_t dacUpdates = 0;
static volatile uint32_t bufferUnderruns = 0;
static volatile uint32_t bufferOverruns = 0;
static uint32_t lastDacCount = 0;
static uint64_t lastMeasureTime = 0;

/**
 * @brief Convert float audio sample (-1.0 to +1.0) to 12-bit DAC value
 */
static inline uint16_t audioToDAC(float sample)
{
    // Clamp sample to valid range
    if (sample > 1.0f)
        sample = 1.0f;
    if (sample < -1.0f)
        sample = -1.0f;

    // Convert from [-1.0, +1.0] to [0, 4095]
    return (uint16_t)((sample + 1.0f) * 2047.5f);
}

/**
 * @brief Get number of samples available in ring buffer
 */
static inline uint32_t ringBufferAvailable(void)
{
    return (writeIndex - readIndex) & RING_BUFFER_MASK;
}

/**
 * @brief Get free space in ring buffer
 */
static inline uint32_t ringBufferFree(void)
{
    return RING_BUFFER_SIZE - ringBufferAvailable() - 1;
}

/**
 * @brief Timer IRQ handler - Queues DMA transfers for DAC updates
 *
 * EXECUTION TIME: <1μs (critical for 44.1kHz = 22.68μs period)
 *
 * OPERATION:
 * 1. Check if DMA channel is available (not busy with previous transfer)
 * 2. Read next sample from ring buffer
 * 3. Format sample as I2C command sequence for MCP4725
 * 4. Queue DMA transfer (returns immediately - hardware handles I2C)
 * 5. Schedule next interrupt
 *
 * I2C DATA_CMD REGISTER FORMAT (16-bit writes to RP2350 I2C peripheral):
 * Each 16-bit value = [Control Bits : Data Byte]
 * Bit 15-10: Reserved
 * Bit 9 (0x200): STOP - Generate I2C STOP condition after this byte
 * Bit 8 (0x100): RESTART - Generate I2C RESTART before this byte
 * Bit 7-0: Data byte to transmit
 *
 * MCP4725 FAST WRITE COMMAND (3 bytes):
 * Byte 0: 0x40 = Write DAC register command
 * Byte 1: D11-D4 = Upper 8 bits of 12-bit value
 * Byte 2: D3-D0<<4 = Lower 4 bits, left-justified (bits 7-4)
 *
 * DMA TRANSFER:
 * - Hardware automatically feeds I2C TX FIFO using DREQ signal
 * - I2C peripheral handles START, address, ACK, STOP automatically
 * - Transfer completes in ~12μs at 2MHz I2C (24 bits / 2MHz)
 * - CPU is free to process audio during transfer
 */
static void __isr timerCallback(void)
{
    gpio_put(TEST_PIN, 1);

    // Clear interrupt
    hw_clear_bits(&timer_hw->intr, 1u << 0);

    if (readIndex != writeIndex && !dma_channel_is_busy(dma_chan))
    {
        uint16_t dacValue = ringBuffer[readIndex];
        readIndex = (readIndex + 1) & RING_BUFFER_MASK;

        dma_i2c_buffer[0] = 0x40;
        dma_i2c_buffer[1] = (dacValue >> 4) & 0xFF;
        dma_i2c_buffer[2] = ((dacValue << 4) & 0xF0) | 0x200;

        dma_channel_set_read_addr(dma_chan, dma_i2c_buffer, true);
        dacUpdates++;
    }
    else if (readIndex != writeIndex)
    {
        bufferUnderruns++;
    }
    else
    {
        bufferUnderruns++;
    }

    // Schedule next interrupt
    timer_hw->alarm[0] = timer_hw->timerawl + TIMER_PERIOD_US;

    gpio_put(TEST_PIN, 0);
}

int main()
{
    stdio_init_all();

    // Note: RP2350 hardware FPU is automatically enabled by Pico SDK
    // Heavy's DSP processing will use hardware floating-point instructions

    sleep_ms(2000); // Wait for USB serial

    printf("\n=== Timer-Driven 440Hz Tone Test ===\n");
    printf("Hardware FPU: Enabled (Cortex-M33 FPv5)\n");
    printf("FPU: Hardware floating-point enabled\n");
    printf("DAC Sample Rate: %d Hz (Hardware Timer)\n", DAC_SAMPLE_RATE);
    printf("Heavy Sample Rate: %.0f Hz\n", HEAVY_SAMPLE_RATE);
    printf("Ring Buffer Size: %d samples\n", RING_BUFFER_SIZE);

    // Initialize LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

    // Initialize TEST_PIN for performance profiling
    gpio_init(TEST_PIN);
    gpio_set_dir(TEST_PIN, GPIO_OUT);
    gpio_put(TEST_PIN, 0);

    // Initialize DAC
    printf("\nInitializing MCP4725 DAC...\n");
    if (!dac.init())
    {
        printf("ERROR: Failed to initialize DAC!\n");
        while (1)
        {
            gpio_put(LED_PIN, !gpio_get(LED_PIN));
            sleep_ms(100);
        }
    }
    printf("DAC initialized successfully.\n");

    // Test DAC with a few values
    printf("\nTesting DAC output...\n");
    printf("  Setting to 2.5V (DAC=2048)...\n");
    dac.setRaw(2048, false);
    sleep_ms(500);

    // Initialize Heavy audio engine
    printf("\nInitializing Heavy audio engine...\n");
    heavyContext = hv_440tone_new(HEAVY_SAMPLE_RATE);
    if (!heavyContext)
    {
        printf("ERROR: Failed to create Heavy context!\n");
        while (1)
        {
            gpio_put(LED_PIN, !gpio_get(LED_PIN));
            sleep_ms(100);
        }
    }

    printf("Heavy context created:\n");
    printf("  Sample rate: %.0f Hz\n", hv_getSampleRate(heavyContext));
    printf("  Input channels: %d\n", hv_getNumInputChannels(heavyContext));
    printf("  Output channels: %d\n", hv_getNumOutputChannels(heavyContext));

    // Test Heavy output
    printf("\nTesting Heavy engine output...\n");
    hv_processInline(heavyContext, NULL, audioBuffer, BUFFER_SIZE);
    printf("First 8 samples (Left channel):\n");
    for (int i = 0; i < 8; i++)
    {
        uint16_t dacVal = audioToDAC(audioBuffer[i]);
        printf("  [%d] %.4f -> DAC=%u\n", i, audioBuffer[i], dacVal);
    }

    // Pre-fill ring buffer with some samples to prevent initial underrun
    printf("\nPre-filling ring buffer...\n");
    for (int buf = 0; buf < 4; buf++)
    {
        hv_processInline(heavyContext, NULL, audioBuffer, BUFFER_SIZE);
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            ringBuffer[writeIndex] = audioToDAC(audioBuffer[i]);
            writeIndex = (writeIndex + 1) & RING_BUFFER_MASK;
        }
    }
    printf("Ring buffer pre-filled with %lu samples.\n", ringBufferAvailable());

    // ============================================================================
    // DMA SETUP FOR NON-BLOCKING I2C TRANSFERS
    // ============================================================================
    // DMA enables 44.1kHz operation by offloading I2C communication to hardware.
    // The CPU only queues transfers (<1μs), while DMA handles the actual I2C
    // protocol (START, address, data, STOP) asynchronously.

    printf("\nSetting up DMA for I2C...\n");

    // Configure I2C peripheral with target address (done once at startup)
    // The I2C peripheral remembers the target address for all transfers
    i2c_hw_t *i2c_hw = i2c_get_hw(DAC_I2C_PORT);
    i2c_hw->enable = 0;            // Disable to modify settings
    i2c_hw->tar = DAC_I2C_ADDRESS; // Set target address (0x60)
    i2c_hw->enable = 1;            // Re-enable

    // Claim an unused DMA channel for our I2C transfers
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dma_cfg = dma_channel_get_default_config(dma_chan);

    // DMA CONFIGURATION:
    // - Transfer size: 16-bit (matches I2C data_cmd register width)
    // - Read increment: YES (read successive bytes from our buffer)
    // - Write increment: NO (always write to same I2C register)
    // - DREQ: I2C TX (DMA waits for I2C FIFO to have space)
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, i2c_get_dreq(DAC_I2C_PORT, true)); // TX DREQ

    // Configure the DMA channel:
    // - Write to: I2C data_cmd register (feeds TX FIFO)
    // - Read from: dma_i2c_buffer (set per-transfer in interrupt)
    // - Count: 3 words (16-bit each) = MCP4725 command + 2 data bytes
    // - Don't start yet: transfers are triggered from timer interrupt
    dma_channel_configure(
        dma_chan,
        &dma_cfg,
        &i2c_hw->data_cmd, // Destination: I2C data_cmd register
        NULL,              // Source: set dynamically in interrupt
        3,                 // Transfer count: 3 x 16-bit words
        false              // Don't start automatically
    );
    printf("DMA channel %d configured for I2C.\n", dma_chan);
    printf("  Transfer size: 16-bit (I2C data_cmd register)\n");
    printf("  DREQ: I2C1 TX (hardware paced)\n");
    printf("  Target: 0x%02X @ 2MHz I2C (~12μs per transfer)\n", DAC_I2C_ADDRESS);

    // Set up hardware timer interrupt
    printf("\nSetting up hardware timer...\n");

    // Enable timer IRQ
    irq_set_exclusive_handler(TIMER0_IRQ_0, timerCallback);
    irq_set_enabled(TIMER0_IRQ_0, true);

    // Enable timer alarm interrupt
    hw_set_bits(&timer_hw->inte, 1u << 0);

    // Set first alarm
    timer_hw->alarm[0] = timer_hw->timerawl + TIMER_PERIOD_US;

    printf("Timer interrupt enabled at %d Hz.\n", DAC_SAMPLE_RATE);

    printf("\n=== Starting Audio Loop ===\n");
    printf("Generating 440Hz tone with timer-driven DAC updates...\n");
    printf("Press Ctrl+C to stop.\n\n");

    uint32_t lastPrintTime = 0;

    while (true)
    {
        // Check if ring buffer needs refilling
        uint32_t available = ringBufferAvailable();
        uint32_t free = ringBufferFree();

        // Keep buffer topped up - generate samples when below watermark (50% = 256 samples)
        // This prevents boom/bust cycles and reduces underruns
        if (available < BUFFER_LOW_WATERMARK)
        {
            // Process audio from Heavy
            hv_processInline(heavyContext, NULL, audioBuffer, BUFFER_SIZE);
            samplesGenerated += BUFFER_SIZE;

            // Convert and write to ring buffer (use left channel)
            for (int i = 0; i < BUFFER_SIZE; i++)
            {
                uint32_t nextWrite = (writeIndex + 1) & RING_BUFFER_MASK;

                // Check for buffer overrun (should never happen with our logic)
                if (nextWrite == readIndex)
                {
                    bufferOverruns++;
                    break;
                }

                ringBuffer[writeIndex] = audioToDAC(audioBuffer[i]);
                writeIndex = nextWrite;
            }
        }
        else
        {
            // Ring buffer is full, wait a bit
            sleep_us(500);
        }

        // Print status every second
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - lastPrintTime >= 1000)
        {
            uint32_t buffered = ringBufferAvailable();
            float fillPercent = (buffered * 100.0f) / RING_BUFFER_SIZE;

            // Calculate actual DAC update rate
            uint64_t currentTime = time_us_64();
            uint32_t dacDelta = dacUpdates - lastDacCount;
            float elapsedSec = (currentTime - lastMeasureTime) / 1000000.0f;
            float actualDacRate = dacDelta / elapsedSec;
            float predictedFreq = 440.0f * (actualDacRate / HEAVY_SAMPLE_RATE);

            printf("DAC: %lu (%.0f Hz actual) | Heavy: %.0f Hz | Freq: %.1f Hz | Buffer: %lu (%.1f%%) | U/O: %lu/%lu\n",
                   dacUpdates, actualDacRate, HEAVY_SAMPLE_RATE, predictedFreq,
                   buffered, fillPercent, bufferUnderruns, bufferOverruns);

            lastDacCount = dacUpdates;
            lastMeasureTime = currentTime;
            lastPrintTime = now;
            gpio_put(LED_PIN, !gpio_get(LED_PIN));
        }
    }

    return 0;
}
