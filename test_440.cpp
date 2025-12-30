/**
 * @file test_440.cpp
 * @brief 440Hz Tone Generator - Simple Direct Test
 * @author Ale Moglia
 * @date 30 December 2025
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "Heavy_440tone.h"
#include "lib/hardware.h"
#include "lib/dac/MCP4725.h"

// Audio configuration
// Adjusted sample rate to achieve exactly 440Hz output
// Compensates for actual I2C DAC timing (~9.5kHz effective rate)
#define SAMPLE_RATE 10960.0f
#define BUFFER_SIZE 32

// Audio buffers
static float audioBuffer[BUFFER_SIZE * 2]; // Stereo output from Heavy

// Heavy context
static HeavyContextInterface *heavyContext = NULL;

// MCP4725 DAC instance
static MCP4725 dac;

// Statistics
static uint32_t samplesGenerated = 0;
static uint32_t dacUpdates = 0;

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

int main()
{
    stdio_init_all();
    sleep_ms(2000); // Wait for USB serial

    printf("\n=== Simple 440Hz Tone Test ===\n");
    printf("Sample Rate: %.0f Hz (matched to I2C capability)\n", SAMPLE_RATE);

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
    printf("  Setting to 0V (DAC=0)...\n");
    dac.setRaw(0, false);
    sleep_ms(500);
    printf("  Setting to 2.5V (DAC=2048)...\n");
    dac.setRaw(2048, false);
    sleep_ms(500);
    printf("  Setting to 5V (DAC=4095)...\n");
    dac.setRaw(4095, false);
    sleep_ms(500);
    printf("  Setting to 2.5V (DAC=2048)...\n");
    dac.setRaw(2048, false);
    sleep_ms(500);

    // Initialize Heavy audio engine
    printf("\nInitializing Heavy audio engine...\n");
    heavyContext = hv_440tone_new(SAMPLE_RATE);
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
        printf("  [%d] %.4f -> DAC=%u (%.2fV)\n",
               i, audioBuffer[i], dacVal, (dacVal * 5.0f) / 4095.0f);
    }

    // Check if output is all zeros
    bool hasSignal = false;
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        if (audioBuffer[i] != 0.0f)
        {
            hasSignal = true;
            break;
        }
    }

    if (!hasSignal)
    {
        printf("\nWARNING: Heavy is producing all zeros! Check patch.\n");
    }
    else
    {
        printf("\nHeavy is generating audio signal.\n");
    }

    printf("\n=== Starting Audio Loop ===\n");
    printf("Generating 440Hz tone and updating DAC...\n");
    printf("UART output disabled to prevent audio glitches.\n\n");

    while (true)
    {
        // Process a buffer of audio
        hv_processInline(heavyContext, NULL, audioBuffer, BUFFER_SIZE);
        samplesGenerated += BUFFER_SIZE;

        // Update DAC with every sample
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            // Use left channel (first half of buffer)
            float sample = audioBuffer[i];
            uint16_t dacValue = audioToDAC(sample);

            // Direct DAC write
            dac.setRaw(dacValue, false);
            dacUpdates++;
        }

        // Blink LED periodically without printing
        if ((samplesGenerated % 10000) == 0)
        {
            gpio_put(LED_PIN, !gpio_get(LED_PIN));
        }
    }

    return 0;
}
