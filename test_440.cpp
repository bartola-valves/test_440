/**
 * @file test_440.cpp
 * @brief 440Hz Tone Generator - Timer-Driven DAC Updates
 * @author Ale Moglia
 * @date 30 December 2025
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/irq.h"

#include "Heavy_440tone.h"
#include "lib/hardware.h"
#include "lib/dac/MCP4725.h"

// Audio configuration
#define DAC_SAMPLE_RATE 44100      // Timer rate at 44.1kHz
#define HEAVY_SAMPLE_RATE 44100.0f // MUST match actual DAC rate for correct 440Hz
#define BUFFER_SIZE 64             // Heavy processing block size

// Ring buffer configuration
#define RING_BUFFER_SIZE 512 // Power of 2 for efficient modulo
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

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
 * @brief Timer IRQ handler - Updates DAC at precise intervals
 */
static void __isr timerCallback(void)
{
    hw_clear_bits(&timer_hw->intr, 1u << 0);

    if (readIndex != writeIndex)
    {
        uint16_t dacValue = ringBuffer[readIndex];
        readIndex = (readIndex + 1) & RING_BUFFER_MASK;
        dac.setRaw(dacValue, false);
        dacUpdates++;
    }
    else
    {
        bufferUnderruns++;
    }

    timer_hw->alarm[0] = timer_hw->timerawl + (1000000 / DAC_SAMPLE_RATE);
}

int main()
{
    stdio_init_all();
    sleep_ms(2000); // Wait for USB serial

    printf("\n=== Timer-Driven 440Hz Tone Test ===\n");
    printf("DAC Sample Rate: %d Hz (Hardware Timer)\n", DAC_SAMPLE_RATE);
    printf("Heavy Sample Rate: %.0f Hz\n", HEAVY_SAMPLE_RATE);
    printf("Ring Buffer Size: %d samples\n", RING_BUFFER_SIZE);

    // Initialize LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

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

    // Set up hardware timer interrupt
    printf("\nSetting up hardware timer...\n");

    // Enable timer IRQ
    irq_set_exclusive_handler(TIMER0_IRQ_0, timerCallback);
    irq_set_enabled(TIMER0_IRQ_0, true);

    // Enable timer alarm interrupt
    hw_set_bits(&timer_hw->inte, 1u << 0);

    // Set first alarm (100us for 10kHz)
    timer_hw->alarm[0] = timer_hw->timerawl + (1000000 / DAC_SAMPLE_RATE);

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

        // Refill if we have space for at least one buffer
        if (free >= BUFFER_SIZE)
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
