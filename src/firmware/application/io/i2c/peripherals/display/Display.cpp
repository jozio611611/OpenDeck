/*

Copyright 2015-2022 Igor Petrovic

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#ifdef PROJECT_TARGET_SUPPORT_DISPLAY

#include <string.h>
#include "Display.h"
#include "application/protocol/midi/MIDI.h"
#include "core/MCU.h"
#include "core/util/Util.h"
#include "application/io/common/Common.h"
#include "application/util/conversion/Conversion.h"
#include "application/util/configurable/Configurable.h"

using namespace io;
using namespace protocol;

Display::Display(I2C::Peripheral::HWA& hwa,
                 Database&             database)
    : _hwa(hwa)
    , _database(database)
{
    MIDIDispatcher.listen(messaging::eventType_t::ANALOG,
                          [this](const messaging::event_t& event)
                          {
                              displayEvent(Display::eventType_t::OUT, event);
                          });

    MIDIDispatcher.listen(messaging::eventType_t::BUTTON,
                          [this](const messaging::event_t& event)
                          {
                              displayEvent(Display::eventType_t::OUT, event);
                          });

    MIDIDispatcher.listen(messaging::eventType_t::ENCODER,
                          [this](const messaging::event_t& event)
                          {
                              displayEvent(Display::eventType_t::OUT, event);
                          });

    MIDIDispatcher.listen(messaging::eventType_t::MIDI_IN,
                          [this](const messaging::event_t& event)
                          {
                              if (event.message != MIDI::messageType_t::SYS_EX)
                              {
                                  displayEvent(Display::eventType_t::IN, event);
                              }
                          });

    ConfigHandler.registerConfig(
        sys::Config::block_t::I2C,
        // read
        [this](uint8_t section, size_t index, uint16_t& value)
        {
            return sysConfigGet(static_cast<sys::Config::Section::i2c_t>(section), index, value);
        },

        // write
        [this](uint8_t section, size_t index, uint16_t value)
        {
            return sysConfigSet(static_cast<sys::Config::Section::i2c_t>(section), index, value);
        });

    I2C::registerPeripheral(this);
}

bool Display::init()
{
    if (!_hwa.init())
    {
        return false;
    }

    bool addressFound = false;

    for (size_t address = 0; address < I2C_ADDRESS.size(); address++)
    {
        if (_hwa.deviceAvailable(I2C_ADDRESS[address]))
        {
            addressFound        = true;
            _selectedI2Caddress = I2C_ADDRESS[address];
            break;
        }
    }

    if (!addressFound)
    {
        return false;
    }

    if (_database.read(database::Config::Section::i2c_t::DISPLAY, setting_t::ENABLE))
    {
        auto controller = static_cast<displayController_t>(_database.read(database::Config::Section::i2c_t::DISPLAY, setting_t::CONTROLLER));
        auto resolution = static_cast<displayResolution_t>(_database.read(database::Config::Section::i2c_t::DISPLAY, setting_t::RESOLUTION));

        if (initU8X8(_selectedI2Caddress, controller, resolution))
        {
            _resolution  = resolution;
            _initialized = true;

            if (!_startupInfoShown)
            {
                if (_database.read(database::Config::Section::i2c_t::DISPLAY, setting_t::DEVICE_INFO_MSG) && !_startupInfoShown)
                {
                    displayWelcomeMessage();
                    u8x8_ClearDisplay(&_u8x8);
                }
            }

            setRetentionTime(_database.read(database::Config::Section::i2c_t::DISPLAY, setting_t::EVENT_TIME) * 1000);

            clearEvent(eventType_t::IN);
            clearEvent(eventType_t::OUT);
        }
        else
        {
            _initialized = false;
            return false;
        }
    }

    // make sure welcome message on startup isn't shown anymore when init is called
    _startupInfoShown = true;

    return _initialized;
}

bool Display::initU8X8(uint8_t i2cAddress, displayController_t controller, displayResolution_t resolution)
{
    bool success = false;

    // setup defaults
    u8x8_SetupDefaults(&_u8x8);
    _u8x8.user_ptr = this;

    // i2c hw access
    auto gpioDelay = [](u8x8_t* u8x8, uint8_t msg, uint8_t argInt, U8X8_UNUSED void* argPtr) -> uint8_t
    {
        return 0;
    };

    auto i2cHWA = [](u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr) -> uint8_t
    {
        auto instance = static_cast<Display*>(u8x8->user_ptr);
        auto data     = static_cast<uint8_t*>(argPtr);

        switch (msg)
        {
        case U8X8_MSG_BYTE_SEND:
        {
            memcpy(&instance->_u8x8Buffer[instance->_u8x8Counter], data, argInt);
            instance->_u8x8Counter += argInt;
        }
        break;

        case U8X8_MSG_BYTE_INIT:
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
        {
            instance->_u8x8Counter = 0;
        }
        break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            return instance->_hwa.write(u8x8_GetI2CAddress(u8x8), instance->_u8x8Buffer, instance->_u8x8Counter);

        default:
            return 0;
        }

        return 1;
    };

    // setup specific callbacks depending on controller/resolution
    if ((resolution == displayResolution_t::R128X64) && (controller == displayController_t::SSD1306))
    {
        _u8x8.display_cb        = u8x8_d_ssd1306_128x64_noname;
        _u8x8.cad_cb            = u8x8_cad_ssd13xx_i2c;
        _u8x8.byte_cb           = i2cHWA;
        _u8x8.gpio_and_delay_cb = gpioDelay;
        _rows                   = 4;
        success                 = true;
    }
    else if ((resolution == displayResolution_t::R128X32) && (controller == displayController_t::SSD1306))
    {
        _u8x8.display_cb        = u8x8_d_ssd1306_128x32_univision;
        _u8x8.cad_cb            = u8x8_cad_ssd13xx_i2c;
        _u8x8.byte_cb           = i2cHWA;
        _u8x8.gpio_and_delay_cb = gpioDelay;
        _rows                   = 2;
        success                 = true;
    }

    if (success)
    {
        _u8x8.i2c_address = i2cAddress;
        u8x8_SetupMemory(&_u8x8);
        u8x8_InitDisplay(&_u8x8);
        u8x8_SetFont(&_u8x8, u8x8_font_pxplustandynewtv_r);
        u8x8_ClearDisplay(&_u8x8);
        u8x8_SetPowerSave(&_u8x8, false);

        return true;
    }

    return false;
}

bool Display::deInit()
{
    if (!_initialized)
    {
        return false;    // nothing to do
    }

    u8x8_SetupDefaults(&_u8x8);

    _rows        = 0;
    _initialized = false;

    return true;
}

/// Checks if LCD requires updating continuously.
void Display::update()
{
    if (!_initialized)
    {
        return;
    }

    if ((core::mcu::timing::ms() - _lastLCDupdateTime) < LCD_REFRESH_TIME)
    {
        return;    // we don't need to update lcd in real time
    }

    for (int i = 0; i < MAX_ROWS; i++)
    {
        if (!_charChange[i])
        {
            continue;
        }

        auto&     row      = _lcdRowText[i];
        const int STR_SIZE = strlen(&row[0]);

        for (int j = 0; j < STR_SIZE; j++)
        {
            if (core::util::BIT_READ(_charChange[i], j))
            {
                u8x8_DrawGlyph(&_u8x8, j, ROW_MAP[_resolution][i], row[j]);
            }
        }

        _charChange[i] = 0;
    }

    _lastLCDupdateTime = core::mcu::timing::ms();

    // check if in/out messages need to be cleared
    if (_messageRetentionTime)
    {
        for (int i = 0; i < 2; i++)
        {
            // 0 = in, 1 = out
            if ((core::mcu::timing::ms() - _lasMessageDisplayTime[i] > _messageRetentionTime) && _messageDisplayed[i])
            {
                clearEvent(static_cast<eventType_t>(i));
            }
        }
    }
}

/// Updates text to be shown on display.
/// This function only updates internal buffers with received text, actual updating is done in update() function.
/// Text isn't passed directly, instead, value from string builder is used.
/// param [in]: row             Row which is being updated.
void Display::updateText(uint8_t row)
{
    if (!_initialized)
    {
        return;
    }

    auto string = _stringBuilder.string();
    auto size   = strlen(string);

    for (size_t i = 0; i < size; i++)
    {
        if (_lcdRowText[row][i] != string[i])
        {
            core::util::BIT_SET(_charChange[row], i);
        }

        _lcdRowText[row][i] = string[i];
    }
}

/// Calculates position on which text needs to be set on display to be in center of display row.
/// param [in]: textSize    Size of text for which center position on display is being calculated.
/// returns: Center position of text on display.
uint8_t Display::getTextCenter(uint8_t textSize)
{
    return MAX_COLUMNS / 2 - (textSize / 2);
}

/// Sets new message retention time.
/// param [in]: retentionTime New retention time in milliseconds.
void Display::setRetentionTime(uint32_t retentionTime)
{
    if (retentionTime < _messageRetentionTime)
    {
        for (int i = 0; i < 2; i++)
        {
            // 0 = in, 1 = out
            // make sure events are cleared immediately in next call of update()
            _lasMessageDisplayTime[i] = 0;
        }
    }

    _messageRetentionTime = retentionTime;

    // reset last update time
    _lasMessageDisplayTime[eventType_t::IN]  = core::mcu::timing::ms();
    _lasMessageDisplayTime[eventType_t::OUT] = core::mcu::timing::ms();
}

/// Adds normalization to a given octave.
int8_t Display::normalizeOctave(uint8_t octave, int8_t normalization)
{
    return static_cast<int8_t>(octave) + normalization;
}

void Display::displayWelcomeMessage()
{
    if (!_initialized)
    {
        return;
    }

    uint8_t startRow;
    bool    showBoard = false;

    u8x8_ClearDisplay(&_u8x8);

    switch (_rows)
    {
    case 4:
    {
        startRow  = 1;
        showBoard = true;
    }
    break;

    default:
    {
        startRow = 0;
    }
    break;
    }

    auto writeString = [&](const char* string, uint8_t row)
    {
        uint8_t charIndex = 0;
        uint8_t location  = getTextCenter(strlen(string));

        while (string[charIndex] != '\0')
        {
            u8x8_DrawGlyph(&_u8x8, location + charIndex, ROW_MAP[_resolution][startRow], string[charIndex]);
            charIndex++;
        }
    };

    _stringBuilder.overwrite("OpenDeck");
    writeString(_stringBuilder.string(), startRow);

    startRow++;
    _stringBuilder.overwrite("FW: v%d.%d.%d", SW_VERSION_MAJOR, SW_VERSION_MINOR, SW_VERSION_REVISION);
    writeString(_stringBuilder.string(), startRow);

    if (showBoard)
    {
        startRow++;
        _stringBuilder.overwrite("HW: %s", Strings::TARGET_NAME_STRING);
        writeString(_stringBuilder.string(), startRow);
    }

    core::mcu::timing::waitMs(2000);
}

void Display::displayEvent(eventType_t type, const messaging::event_t& event)
{
    if (!_initialized)
    {
        return;
    }

    uint8_t startRow = (type == Display::eventType_t::IN) ? ROW_START_IN_MESSAGE : ROW_START_OUT_MESSAGE;

    _stringBuilder.overwrite("%s%s",
                             (type == Display::eventType_t::IN)
                                 ? Strings::IN_EVENT_STRING
                                 : Strings::OUT_EVENT_STRING,
                             Strings::MIDI_MESSAGE(event.message));
    _stringBuilder.fillUntil((type == Display::eventType_t::IN)
                                 ? MAX_COLUMNS_IN_MESSAGE
                                 : MAX_COLUMNS_OUT_MESSAGE);
    updateText(startRow);

    switch (event.message)
    {
    case MIDI::messageType_t::NOTE_OFF:
    case MIDI::messageType_t::NOTE_ON:
    {
        if (!_database.read(database::Config::Section::i2c_t::DISPLAY, setting_t::MIDI_NOTES_ALTERNATE))
        {
            _stringBuilder.overwrite("%d", event.index);
        }
        else
        {
            _stringBuilder.overwrite("%s%d",
                                     Strings::NOTE(MIDI::NOTE_TO_TONIC(event.index)),
                                     normalizeOctave(MIDI::NOTE_TO_OCTAVE(event.value), _octaveNormalization));
        }

        _stringBuilder.append(" v%d CH%d", event.value, event.channel);
        _stringBuilder.fillUntil(MAX_COLUMNS);
        updateText(startRow + 1);
    }
    break;

    case MIDI::messageType_t::PROGRAM_CHANGE:
    {
        _stringBuilder.overwrite("%d CH%d", event.index, event.channel);
        _stringBuilder.fillUntil(MAX_COLUMNS);
        updateText(startRow + 1);
    }
    break;

    case MIDI::messageType_t::CONTROL_CHANGE:
    case MIDI::messageType_t::CONTROL_CHANGE_14BIT:
    case MIDI::messageType_t::NRPN_7BIT:
    case MIDI::messageType_t::NRPN_14BIT:
    {
        _stringBuilder.overwrite("%d %d CH%d", event.index, event.value, event.channel);
        _stringBuilder.fillUntil(MAX_COLUMNS);
        updateText(startRow + 1);
    }
    break;

    case MIDI::messageType_t::MMC_PLAY:
    case MIDI::messageType_t::MMC_STOP:
    case MIDI::messageType_t::MMC_RECORD_START:
    case MIDI::messageType_t::MMC_RECORD_STOP:
    case MIDI::messageType_t::MMC_PAUSE:
    {
        _stringBuilder.overwrite("CH%d", event.index);
        _stringBuilder.fillUntil(MAX_COLUMNS);
        updateText(startRow + 1);
    }
    break;

    case MIDI::messageType_t::SYS_REAL_TIME_CLOCK:
    case MIDI::messageType_t::SYS_REAL_TIME_START:
    case MIDI::messageType_t::SYS_REAL_TIME_CONTINUE:
    case MIDI::messageType_t::SYS_REAL_TIME_STOP:
    case MIDI::messageType_t::SYS_REAL_TIME_ACTIVE_SENSING:
    case MIDI::messageType_t::SYS_REAL_TIME_SYSTEM_RESET:
    case MIDI::messageType_t::SYS_EX:
    {
        _stringBuilder.overwrite("");
        _stringBuilder.fillUntil(MAX_COLUMNS);
        updateText(startRow + 1);
    }
    break;

    default:
        break;
    }

    _lasMessageDisplayTime[type] = core::mcu::timing::ms();
    _messageDisplayed[type]      = true;
}

void Display::clearEvent(eventType_t type)
{
    if (!_initialized)
    {
        return;
    }

    switch (type)
    {
    case eventType_t::IN:
    {
        // first row
        _stringBuilder.overwrite(Strings::IN_EVENT_STRING);
        _stringBuilder.fillUntil(MAX_COLUMNS_IN_MESSAGE);
        updateText(ROW_START_IN_MESSAGE);
        // second row
        _stringBuilder.overwrite("");
        _stringBuilder.fillUntil(MAX_COLUMNS);
        updateText(ROW_START_IN_MESSAGE + 1);
    }
    break;

    case eventType_t::OUT:
    {
        // first row
        _stringBuilder.overwrite(Strings::OUT_EVENT_STRING);
        _stringBuilder.fillUntil(MAX_COLUMNS_IN_MESSAGE);
        updateText(ROW_START_OUT_MESSAGE);
        // second row
        _stringBuilder.overwrite("");
        _stringBuilder.fillUntil(MAX_COLUMNS);
        updateText(ROW_START_OUT_MESSAGE + 1);
    }
    break;

    default:
        return;
    }

    _messageDisplayed[type] = false;
}

std::optional<uint8_t> Display::sysConfigGet(sys::Config::Section::i2c_t section, size_t index, uint16_t& value)
{
    if (section != sys::Config::Section::i2c_t::DISPLAY)
    {
        return std::nullopt;
    }

    uint32_t readValue;

    auto result = _database.read(util::Conversion::SYS_2_DB_SECTION(section), index, readValue)
                      ? sys::Config::status_t::ACK
                      : sys::Config::status_t::ERROR_READ;

    value = readValue;

    return result;
}

std::optional<uint8_t> Display::sysConfigSet(sys::Config::Section::i2c_t section, size_t index, uint16_t value)
{
    if (section != sys::Config::Section::i2c_t::DISPLAY)
    {
        return std::nullopt;
    }

    auto initAction = common::initAction_t::AS_IS;

    switch (section)
    {
    case sys::Config::Section::i2c_t::DISPLAY:
    {
        auto setting = static_cast<setting_t>(index);

        switch (setting)
        {
        case setting_t::ENABLE:
        {
            initAction = value ? common::initAction_t::INIT : common::initAction_t::DE_INIT;
        }
        break;

        case setting_t::CONTROLLER:
        {
            if ((value <= static_cast<uint8_t>(displayController_t::AMOUNT)) && (value >= 0))
            {
                initAction = common::initAction_t::INIT;
            }
        }
        break;

        case setting_t::RESOLUTION:
        {
            if ((value <= static_cast<uint8_t>(displayResolution_t::AMOUNT)) && (value >= 0))
            {
                initAction = common::initAction_t::INIT;
            }
        }
        break;

        case setting_t::EVENT_TIME:
        {
            setRetentionTime(value * 1000);
        }
        break;

        default:
            break;
        }
    }
    break;

    default:
        break;
    }

    auto result = _database.update(util::Conversion::SYS_2_DB_SECTION(section), index, value)
                      ? sys::Config::status_t::ACK
                      : sys::Config::status_t::ERROR_WRITE;

    if (result == sys::Config::status_t::ACK)
    {
        if (initAction == common::initAction_t::INIT)
        {
            init();
        }
        else if (initAction == common::initAction_t::DE_INIT)
        {
            deInit();
        }
    }

    return result;
}

#endif