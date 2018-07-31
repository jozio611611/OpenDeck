/*
    OpenDeck MIDI platform firmware
    Copyright (C) 2015-2018 Igor Petrovic

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Handlers.h"
#include "board/common/uart/Variables.h"

///
/// \brief Holds currently active UART channel used for OpenDeck MIDI UART format.
///
uint8_t uartChannel_OD;


///
/// \brief Used to read MIDI data from UART formatted in OpenDeck format.
///
bool usbRead_OD(USBMIDIpacket_t& USBMIDIpacket)
{
    return uartReadMIDI_OD(uartChannel_OD);
}

///
/// \brief Used to write MIDI data to UART formatted in OpenDeck format.
///
bool usbWrite_OD(USBMIDIpacket_t& USBMIDIpacket)
{
    return uartWriteMIDI_OD(uartChannel_OD, USBMIDIpacket);
}

void setupMIDIoverUART_OD(uint8_t channel)
{
    uartChannel_OD = channel;

    #ifdef BOARD_OPEN_DECK
    if (board.isUSBconnected())
    {
        //master board
        //read usb midi data and forward it to uart in od format
        //write standard usb midi data
        //no need for uart handlers
        midi.handleUSBread(usbMIDItoUART_OD);
        midi.handleUSBwrite(board.usbWriteMIDI);
        midi.handleUARTread(NULL); //parsed internally
        midi.handleUARTwrite(NULL);
    }
    else
    {
        //slave
        midi.handleUSBread(usbRead_OD); //loopback used on inner slaves
        midi.handleUSBwrite(usbWrite_OD);
        //no need for uart handlers
        midi.handleUARTread(NULL);
        midi.handleUARTwrite(NULL);
    }
    #elif defined(BOARD_A_MEGA) || defined(BOARD_A_UNO)
    board.initUART(UART_BAUDRATE_MIDI_OD, uartChannel_OD);
    midi.handleUSBread(usbRead_OD);
    midi.handleUSBwrite(usbWrite_OD);
    midi.handleUARTread(NULL);
    midi.handleUARTwrite(NULL);
    #endif
}

bool uartWriteMIDI_OD(uint8_t channel, USBMIDIpacket_t& USBMIDIpacket)
{
    if (channel >= UART_INTERFACES)
        return false;

    RingBuffer_Insert(&txBuffer[channel], 0xF1);
    RingBuffer_Insert(&txBuffer[channel], USBMIDIpacket.Event);
    RingBuffer_Insert(&txBuffer[channel], USBMIDIpacket.Data1);
    RingBuffer_Insert(&txBuffer[channel], USBMIDIpacket.Data2);
    RingBuffer_Insert(&txBuffer[channel], USBMIDIpacket.Data3);
    RingBuffer_Insert(&txBuffer[channel], USBMIDIpacket.Event ^ USBMIDIpacket.Data1 ^ USBMIDIpacket.Data2 ^ USBMIDIpacket.Data3);

    Board::uartTransmitStart(channel);

    return true;
}

bool uartReadMIDI_OD(uint8_t channel)
{
    if (channel >= UART_INTERFACES)
        return false;

    if (RingBuffer_GetCount(&rxBuffer[channel]) >= 6)
    {
        int16_t data = board.uartRead(channel);

        if (data == 0xF1)
        {
            //start of frame, read rest of the packet
            for (int i=0; i<5; i++)
            {
                data = board.uartRead(channel);

                switch(i)
                {
                    case 0:
                    usbMIDIpacket.Event = data;
                    break;

                    case 1:
                    usbMIDIpacket.Data1 = data;
                    break;

                    case 2:
                    usbMIDIpacket.Data2 = data;
                    break;

                    case 3:
                    usbMIDIpacket.Data3 = data;
                    break;

                    case 4:
                    //xor byte, do nothing
                    break;
                }
            }

            //error check
            uint8_t dataXOR = usbMIDIpacket.Event ^ usbMIDIpacket.Data1 ^ usbMIDIpacket.Data2 ^ usbMIDIpacket.Data3;

            return (dataXOR == data);
        }
    }

    return false;
}

bool usbMIDItoUART_OD(USBMIDIpacket_t& USBMIDIpacket)
{
    if (Board::usbReadMIDI(usbMIDIpacket))
    {
        uartWriteMIDI_OD(uartChannel_OD, usbMIDIpacket);
        return true;
    }

    return false;
}
