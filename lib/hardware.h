/*
Unfathomable board

Rev: 0.1
Date: 17th October 2025
(c) 2025 Bartola Ltd. UK
Author: Alejandro Moglia

*/

// Onboard LED on GPIO 25
#define LED_PIN 25
// Test pin for logic analyser connected to GPIO 24
#define TEST_PIN 24
// UART using GPIO 0 and 1
#define UART_TX_PIN 0
#define UART_RX_PIN 1

// 24LC32AT Configuration EEPROM on I2C0 using GPIO20 and GPIO21
#define EEPROM_SDA_PIN 20
#define EEPROM_SCL_PIN 21
#define EEPROM_I2C_PORT i2c0
#define EEPROM_I2C_ADDRESS 0x50 // 24LC32AT I2C address

// DAC MCP4725AOT on I2C1 using GPIO2 and GPIO3
// Address pin 6 (A0) can be tied to GND (0x60) or VCC (0x61)

// DAC output is connected to signal conditioning circuitry to produce -5 to 5V output from the 0 to 5V DAC output
// there is a slew control circuit at the output.

#define DAC_SDA_PIN 2
#define DAC_SCL_PIN 3
#define DAC_I2C_PORT i2c1
#define DAC_I2C_ADDRESS 0x60 // MCP4725AOT I2C address with A0 to GND

// ADC ADC121C027 on I2C1 using GPIO2 and GPIO3
// Address pin can be tied to GND (0x50) or VCC (0x51)
#define ADC_SDA_PIN 2
#define ADC_SCL_PIN 3
#define ADC_I2C_PORT i2c1
#define ADC_I2C_ADDRESS 0x51 // ADC121C027 I2C address (configured A0 to GND)

/*
There are 2 TMUX4051 analog multiplexers on this board.
One is for reading potentiometers (POT MUX) and one is for reading CV/Gate inputs (CV MUX).
Both are connected to the same ADC (ADC121C027) on I2C1 using GPIO2 and GPIO3.
Both multiplexers share the same address pins A0,A1 and A2 connected to GPIO22, GPIO23 and GPIO26 respectively.

The ADC input is controlled by a digital switch DG469 to select the input from either the POT MUX or the CV MUX.
The DG469 control pin is connected to GPIO4.
When the DG469 control pin is LOW, the ADC reads from the POT MUX.
When the DG469 control pin is HIGH, the ADC reads from the CV MUX.

CV inputs are bipolar +/- 5V scaled to 0-5V by external circuitry.
POT inputs are unipolar 0-5V.

CV MUX channels:
Channel 0: Min CV Input
Channel 1: Max CV Input
Channel 2: Attractor CV Input
Channel 3: Entropy CV Input
Channels 4 to 7: Unused

Pot MUX channels:
Channel 0: Min POT
Channel 1: Max POT
Channel 2: Attractor POT
Channel 3: Entropy POT
Channel 4: Clock POT
Channel 5 to 7: Unused

*/
#define MUX_A0_PIN 22
#define MUX_A1_PIN 23
#define MUX_A2_PIN 26
#define CVPOT_CONTROL_PIN 4

// CV/Gate MUX Channels
#define CV_MIN_CHANNEL 0
#define CV_MAX_CHANNEL 1
#define CV_ATTRACTOR_CHANNEL 2
#define CV_ENTROPY_CHANNEL 3

// Pot MUX Channels
#define POT_MIN_CHANNEL 0
#define POT_MAX_CHANNEL 1
#define POT_ATTRACTOR_CHANNEL 2
#define POT_ENTROPY_CHANNEL 3
#define POT_CLOCK_CHANNEL 4

// Sync out on GPIO14
#define SYNC_OUT_PIN 14

// Gate out on GPIO15
#define GATE_OUT_PIN 15

// Loop out on GPIO 17
#define LOOP_OUT_PIN 17

// RGB Status LED on GPIO27 (Red), GPIO28 (Green), GPIO29 (Blue)
// Common anode configuration - LEDs are active-HIGH (1=ON, 0=OFF)
#define RGB_RED_PIN 27
#define RGB_GREEN_PIN 28
#define RGB_BLUE_PIN 29

// 2 switches on the module: Mode and Loop
// Mode switch on GPIO18
// Loop switch on GPIO19

#define MODE_SWITCH_PIN 18
#define LOOP_SWITCH_PIN 19

// 2 gate inputs on module: Loop and CLK
// Loop gate input on GPIO6
// CLK gate input on GPIO5
#define LOOP_GATE_INPUT_PIN 6
#define CLK_GATE_INPUT_PIN 5
