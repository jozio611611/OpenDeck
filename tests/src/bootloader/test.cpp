#ifndef USB_LINK_MCU

#include "unity/Framework.h"
#include "SysExParser/SysExParser.h"
#include "updater/Updater.h"
#include "board/Board.h"
#include "board/Internal.h"
#include "helpers/MIDI.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <iterator>
#include <string>
#include <cstddef>
#include <Target.h>

namespace
{
    class BTLDRWriter : public Updater::BTLDRWriter
    {
        public:
        uint32_t pageSize(size_t index) override
        {
            return Board::detail::map::flashPageDescriptor(index).size;
        }

        void erasePage(size_t index) override
        {
        }

        void fillPage(size_t index, uint32_t address, uint16_t value) override
        {
            writtenBytes.push_back(value & 0xFF);
            writtenBytes.push_back(value >> 8);
        }

        void writePage(size_t index) override
        {
        }

        void apply() override
        {
            updated = true;
        }

        std::vector<uint8_t> writtenBytes = {};
        bool                 updated      = false;
    };

    const std::string fw_build_dir         = "../src/build/";
    const std::string fw_build_type_subdir = "release/";

    SysExParser _sysExParser;
    BTLDRWriter _btldrWriter;
    Updater     _updater = Updater(_btldrWriter, COMMAND_FW_UPDATE_START, COMMAND_FW_UPDATE_END, FW_UID);
}    // namespace

TEST_CASE(Bootloader)
{
    std::string syxPath    = fw_build_dir + BOARD_STRING + "/" + fw_build_type_subdir + "merged/" + BOARD_STRING + ".sysex.syx";
    std::string binaryPath = fw_build_dir + BOARD_STRING + "/" + fw_build_type_subdir + "merged/" + BOARD_STRING + "_sysex.bin";

    std::ifstream        sysExStream(syxPath, std::ios::in | std::ios::binary);
    std::vector<uint8_t> sysExVector((std::istreambuf_iterator<char>(sysExStream)), std::istreambuf_iterator<char>());
    std::ifstream        binaryStream(binaryPath, std::ios::in | std::ios::binary);
    std::vector<uint8_t> binaryVector((std::istreambuf_iterator<char>(binaryStream)), std::istreambuf_iterator<char>());

    std::vector<uint8_t>               singleSysExMsg = {};
    std::vector<MIDI::usbMIDIPacket_t> packets        = {};

    // Go over the entire .syx file.
    // Upon reaching the end of single sysex message, convert it
    // into series of USB MIDI packets.
    for (size_t i = 0; i < sysExVector.size(); i++)
    {
        singleSysExMsg.push_back(sysExVector.at(i));

        if (sysExVector.at(i) == 0xF7)
        {
            auto converted = MIDIHelper::rawSysExToUSBPackets(singleSysExMsg);
            packets.insert(std::end(packets), std::begin(converted), std::end(converted));
            singleSysExMsg.clear();
        }
    }

    // Now we have the entire file in form of USB MIDI packets.
    // Parse each message and once parsing passes, feed the parsed data into FW updater.
    for (size_t packet = 0; packet < packets.size(); packet++)
    {
        if (_sysExParser.isValidMessage(packets.at(packet)))
        {
            size_t  dataSize = _sysExParser.dataBytes();
            uint8_t data     = 0;

            if (dataSize)
            {
                for (size_t i = 0; i < dataSize; i++)
                {
                    if (_sysExParser.value(i, data))
                    {
                        _updater.feed(data);
                    }
                }
            }
        }
    }

    // once all data has been fed into updater, firmware update procedure should be complete
    TEST_ASSERT(_btldrWriter.updated == true);

    // written content should also match the original binary file from which .syx file has been created
    TEST_ASSERT(_btldrWriter.writtenBytes == binaryVector);
}

#endif