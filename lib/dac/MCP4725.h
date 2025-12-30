/**
 * @file MCP4725.h
 * @brief MCP4725 12-bit I2C DAC Class for Raspberry Pi Pico
 * @author Ale Moglia
 * @date 18/10/2025
 * Version: 0.1
 *
 * Object-oriented library for interfacing with the MCP4725 12-bit DAC via I2C
 */

#ifndef MCP4725_H
#define MCP4725_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "../hardware.h"

/**
 * @brief MCP4725 DAC Class
 *
 * This class provides an interface to the MCP4725 12-bit I2C DAC
 * The DAC output is 0-5V which is then conditioned to -5V to +5V by external circuitry
 */
class MCP4725
{
public:
    /**
     * @brief Power down modes for the DAC
     */
    enum PowerDownMode
    {
        POWER_DOWN_OFF = 0,  ///< Normal operation (powered on)
        POWER_DOWN_1K = 1,   ///< Power down with 1kΩ to ground
        POWER_DOWN_100K = 2, ///< Power down with 100kΩ to ground
        POWER_DOWN_500K = 3  ///< Power down with 500kΩ to ground
    };

    /**
     * @brief Constructor
     */
    MCP4725();

    /**
     * @brief Destructor
     */
    ~MCP4725();

    /**
     * @brief Initialize the DAC
     * @return true if initialization successful, false otherwise
     */
    bool init();

    /**
     * @brief Deinitialize the DAC
     */
    void deinit();

    /**
     * @brief Set DAC output using raw 12-bit value (0-4095)
     * @param value Raw 12-bit DAC value (0-4095)
     * @param writeEEPROM If true, write to EEPROM (persists after power cycle)
     * @return true if successful, false otherwise
     */
    bool setRaw(uint16_t value, bool writeEEPROM = false);

    /**
     * @brief Set DAC output voltage in millivolts (0-5000mV)
     * @param millivolts Voltage in millivolts (0-5000mV)
     * @param writeEEPROM If true, write to EEPROM (persists after power cycle)
     * @return true if successful, false otherwise
     */
    bool setMillivolts(uint16_t millivolts, bool writeEEPROM = false);

    /**
     * @brief Set DAC output voltage in volts (0.0-5.0V)
     * @param volts Voltage in volts (0.0-5.0V)
     * @param writeEEPROM If true, write to EEPROM (persists after power cycle)
     * @return true if successful, false otherwise
     */
    bool setVolts(float volts, bool writeEEPROM = false);

    /**
     * @brief Set CV output voltage (-5000 to +5000mV, scaled to 0-5V DAC output)
     * @param millivolts CV voltage in millivolts (-5000 to +5000mV)
     * @param writeEEPROM If true, write to EEPROM (persists after power cycle)
     * @return true if successful, false otherwise
     */
    bool setCVMillivolts(int16_t millivolts, bool writeEEPROM = false);

    /**
     * @brief Set power down mode
     * @param mode Power down mode
     * @return true if successful, false otherwise
     */
    bool setPowerDownMode(PowerDownMode mode);

    /**
     * @brief Read current DAC output value and settings
     * @param value Pointer to store current DAC value (can be NULL)
     * @param eepromValue Pointer to store EEPROM value (can be NULL)
     * @param powerDown Pointer to store power down mode (can be NULL)
     * @return true if successful, false otherwise
     */
    bool readStatus(uint16_t *value = nullptr, uint16_t *eepromValue = nullptr,
                    PowerDownMode *powerDown = nullptr);

    /**
     * @brief Get current DAC value (cached from last write)
     * @return Current DAC value (0-4095)
     */
    uint16_t getCurrentValue() const;

    /**
     * @brief Check if DAC is initialized
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const;

    /**
     * @brief Test I2C communication with DAC
     * @return true if communication successful, false otherwise
     */
    bool testCommunication();

private:
    // DAC voltage reference and resolution constants
    static const int32_t DAC_VREF_MV = 5000;    ///< 5V reference in millivolts
    static const int32_t DAC_RESOLUTION = 4096; ///< 12-bit resolution (2^12)

    // MCP4725 Commands
    static const uint8_t CMD_WRITE_DAC = 0x40;        ///< Write DAC register (fast mode)
    static const uint8_t CMD_WRITE_DAC_EEPROM = 0x60; ///< Write DAC and EEPROM

    // Internal state variables
    bool initialized_;
    uint16_t currentValue_;
    PowerDownMode currentPowerMode_;

    /**
     * @brief Write value to DAC
     * @param value 12-bit DAC value
     * @param powerDown Power down mode
     * @param writeEEPROM Write to EEPROM flag
     * @return true if successful, false otherwise
     */
    bool writeDAC(uint16_t value, PowerDownMode powerDown, bool writeEEPROM);
};

#endif // MCP4725_H
