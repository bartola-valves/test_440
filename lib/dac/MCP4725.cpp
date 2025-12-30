/**
 * @file MCP4725.cpp
 * @brief MCP4725 12-bit I2C DAC Class Implementation
 * @author Ale Moglia
 * @date 2025
 */

#include "MCP4725.h"
#include <stdio.h>

MCP4725::MCP4725()
    : initialized_(false), currentValue_(0), currentPowerMode_(POWER_DOWN_OFF)
{
}

MCP4725::~MCP4725()
{
    if (initialized_)
    {
        deinit();
    }
}

bool MCP4725::init()
{
    if (initialized_)
    {
        return true; // Already initialized
    }

    // Initialize I2C for DAC communication
    i2c_init(DAC_I2C_PORT, 400000); // 400kHz I2C

    // Set up I2C pins
    gpio_set_function(DAC_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DAC_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DAC_SDA_PIN);
    gpio_pull_up(DAC_SCL_PIN);

    // Test I2C communication with DAC
    if (!testCommunication())
    {
        printf("MCP4725: Initialization failed - no I2C response\n");
        return false;
    }

    // Read current DAC status
    uint16_t dacValue, eepromValue;
    PowerDownMode pdMode;
    if (readStatus(&dacValue, &eepromValue, &pdMode))
    {
        currentValue_ = dacValue;
        currentPowerMode_ = pdMode;
        printf("MCP4725: Current DAC=%u, EEPROM=%u, PowerDown=%d\n",
               dacValue, eepromValue, pdMode);
    }

    initialized_ = true;
    printf("MCP4725: Initialized successfully at address 0x%02X\n", DAC_I2C_ADDRESS);
    return true;
}

void MCP4725::deinit()
{
    if (!initialized_)
    {
        return;
    }

    // Optionally set DAC to 0V before deinit
    setRaw(0, false);

    initialized_ = false;
    printf("MCP4725: Deinitialized\n");
}

bool MCP4725::setRaw(uint16_t value, bool writeEEPROM)
{
    if (!initialized_)
    {
        return false;
    }

    // Clamp value to 12-bit range
    if (value > 4095)
    {
        value = 4095;
    }

    return writeDAC(value, currentPowerMode_, writeEEPROM);
}

bool MCP4725::setMillivolts(uint16_t millivolts, bool writeEEPROM)
{
    if (!initialized_)
    {
        return false;
    }

    // Clamp to valid range
    if (millivolts > DAC_VREF_MV)
    {
        millivolts = DAC_VREF_MV;
    }

    // Convert millivolts to 12-bit DAC value
    uint16_t value = (millivolts * DAC_RESOLUTION) / DAC_VREF_MV;

    return setRaw(value, writeEEPROM);
}

bool MCP4725::setVolts(float volts, bool writeEEPROM)
{
    if (!initialized_)
    {
        return false;
    }

    // Convert volts to millivolts
    uint16_t millivolts = (uint16_t)(volts * 1000.0f);

    return setMillivolts(millivolts, writeEEPROM);
}

bool MCP4725::setCVMillivolts(int16_t millivolts, bool writeEEPROM)
{
    if (!initialized_)
    {
        return false;
    }

    // Clamp CV to ±5V range
    if (millivolts < -5000)
    {
        millivolts = -5000;
    }
    else if (millivolts > 5000)
    {
        millivolts = 5000;
    }

    // Convert CV range (-5000 to +5000mV) to DAC range (0 to 5000mV)
    // CV = -5000mV → DAC = 0mV
    // CV = 0mV → DAC = 2500mV
    // CV = +5000mV → DAC = 5000mV
    uint16_t dacMillivolts = (millivolts + 5000) / 2;

    return setMillivolts(dacMillivolts, writeEEPROM);
}

bool MCP4725::setPowerDownMode(PowerDownMode mode)
{
    if (!initialized_)
    {
        return false;
    }

    currentPowerMode_ = mode;

    // Write current value with new power mode
    return writeDAC(currentValue_, mode, false);
}

bool MCP4725::readStatus(uint16_t *value, uint16_t *eepromValue, PowerDownMode *powerDown)
{
    if (!initialized_)
    {
        return false;
    }

    uint8_t data[5];
    int result = i2c_read_blocking(DAC_I2C_PORT, DAC_I2C_ADDRESS, data, 5, false);

    if (result != 5)
    {
        return false;
    }

    // Parse response
    // Byte 0: Status bits
    // Bytes 1-2: Current DAC value (12-bit)
    // Bytes 3-4: EEPROM value (12-bit)

    if (value != nullptr)
    {
        *value = ((data[1] << 4) | (data[2] >> 4)) & 0x0FFF;
    }

    if (eepromValue != nullptr)
    {
        *eepromValue = (((data[3] & 0x0F) << 8) | data[4]) & 0x0FFF;
    }

    if (powerDown != nullptr)
    {
        uint8_t pdBits = (data[0] >> 1) & 0x03;
        *powerDown = static_cast<PowerDownMode>(pdBits);
    }

    return true;
}

uint16_t MCP4725::getCurrentValue() const
{
    return currentValue_;
}

bool MCP4725::isInitialized() const
{
    return initialized_;
}

bool MCP4725::testCommunication()
{
    uint8_t testData;
    int result = i2c_read_blocking(DAC_I2C_PORT, DAC_I2C_ADDRESS, &testData, 1, false);

    return (result == 1);
}

bool MCP4725::writeDAC(uint16_t value, PowerDownMode powerDown, bool writeEEPROM)
{
    // Clamp to 12-bit
    value &= 0x0FFF;

    uint8_t data[3];

    if (writeEEPROM)
    {
        // Write to DAC and EEPROM (3 bytes)
        data[0] = CMD_WRITE_DAC_EEPROM | (powerDown << 1);
        data[1] = (value >> 4) & 0xFF; // Upper 8 bits
        data[2] = (value << 4) & 0xF0; // Lower 4 bits
    }
    else
    {
        // Fast write to DAC only (3 bytes)
        data[0] = CMD_WRITE_DAC | (powerDown << 1);
        data[1] = (value >> 4) & 0xFF; // Upper 8 bits
        data[2] = (value << 4) & 0xF0; // Lower 4 bits
    }

    int result = i2c_write_blocking(DAC_I2C_PORT, DAC_I2C_ADDRESS, data, 3, false);

    if (result == 3)
    {
        currentValue_ = value;
        currentPowerMode_ = powerDown;
        return true;
    }

    return false;
}
