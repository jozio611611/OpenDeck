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

#ifdef UART_SUPPORTED

#include <map>
#include "board/Board.h"
#include "board/Internal.h"
#include "core/src/general/Atomic.h"
#include "nrfx_prs.h"
#include "nrfx_uart.h"
#include <Target.h>

namespace
{
    std::map<uint32_t, nrf_uart_baudrate_t> _baudRateMap = {
        {
            1200,
            NRF_UART_BAUDRATE_1200,
        },
        {
            2400,
            NRF_UART_BAUDRATE_2400,
        },
        {
            4800,
            NRF_UART_BAUDRATE_4800,
        },
        {
            9600,
            NRF_UART_BAUDRATE_9600,
        },
        {
            14400,
            NRF_UART_BAUDRATE_14400,
        },
        {
            19200,
            NRF_UART_BAUDRATE_19200,
        },
        {
            28800,
            NRF_UART_BAUDRATE_28800,
        },
        {
            31250,
            NRF_UART_BAUDRATE_31250,
        },
        {
            38400,
            NRF_UART_BAUDRATE_38400,
        },
        {
            56000,
            NRF_UART_BAUDRATE_56000,
        },
        {
            57600,
            NRF_UART_BAUDRATE_57600,
        },
        {
            76800,
            NRF_UART_BAUDRATE_76800,
        },
        {
            115200,
            NRF_UART_BAUDRATE_115200,
        },
        {
            230400,
            NRF_UART_BAUDRATE_230400,
        },
        {
            250000,
            NRF_UART_BAUDRATE_250000,
        },
        {
            460800,
            NRF_UART_BAUDRATE_460800,
        },
        {
            921600,
            NRF_UART_BAUDRATE_921600,
        },
        {
            1000000,
            NRF_UART_BAUDRATE_1000000,
        },
    };

    nrfx_uart_t _uartInstance[MAX_UART_INTERFACES];
}    // namespace

namespace Board
{
    namespace detail
    {
        namespace UART
        {
            namespace ll
            {
                void enableDataEmptyInt(uint8_t channel)
                {
                    // On NRF52, there is no TX Empty interrupt - only TX ready
                    // interrupt, which occurs after the data has been sent.
                    // In order to trigger the interrupt initially, STARTTX task must be triggered
                    // and data needs to be written to TXD register.

                    size_t  remainingBytes;
                    uint8_t value;

                    if (Board::detail::UART::getNextByteToSend(channel, value, remainingBytes))
                    {
                        nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_TXDRDY);
                        nrf_uart_task_trigger(_uartInstance[channel].p_reg, NRF_UART_TASK_STARTTX);
                        nrf_uart_txd_set(_uartInstance[channel].p_reg, value);
                    }
                    else
                    {
                        nrf_uart_task_trigger(_uartInstance[channel].p_reg, NRF_UART_TASK_STOPTX);
                        Board::detail::UART::indicateTxComplete(channel);
                    }
                }

                bool deInit(uint8_t channel)
                {
                    nrf_uart_int_disable(_uartInstance[channel].p_reg, NRF_UART_INT_MASK_RXDRDY | NRF_UART_INT_MASK_TXDRDY | NRF_UART_INT_MASK_ERROR | NRF_UART_INT_MASK_RXTO);
                    NRFX_IRQ_DISABLE(nrfx_get_irq_number((void*)_uartInstance[channel].p_reg));

                    CORE_IO_DEINIT(Board::detail::map::uartPins(channel).tx);
                    CORE_IO_DEINIT(Board::detail::map::uartPins(channel).rx);

                    nrfx_prs_release(_uartInstance[channel].p_reg);

                    return true;
                }

                bool init(uint8_t channel, Board::UART::config_t& config)
                {
                    switch (channel)
                    {
                    case 0:
                    {
                        _uartInstance[channel] = NRFX_UART_INSTANCE(0);
                    }
                    break;

                    default:
                        return false;
                    }

                    if (config.parity == Board::UART::parity_t::odd)
                        return false;

                    if (!_baudRateMap[config.baudRate])
                        return false;

                    if (nrfx_prs_acquire(_uartInstance[channel].p_reg, nrfx_uart_0_irq_handler) != NRFX_SUCCESS)
                        return false;

                    CORE_IO_SET_HIGH(Board::detail::map::uartPins(channel).tx.port, Board::detail::map::uartPins(channel).tx.index);

                    CORE_IO_INIT({ Board::detail::map::uartPins(channel).tx.port,
                                   Board::detail::map::uartPins(channel).tx.index,
                                   core::io::pinMode_t::outputPP,
                                   core::io::pullMode_t::none });

                    CORE_IO_INIT({ Board::detail::map::uartPins(channel).rx.port,
                                   Board::detail::map::uartPins(channel).rx.index,
                                   core::io::pinMode_t::input,
                                   core::io::pullMode_t::none });

                    nrf_uart_config_t nrf_uart_config = {};
                    nrf_uart_config.hwfc              = NRF_UART_HWFC_DISABLED;
                    nrf_uart_config.parity            = config.parity ? NRF_UART_PARITY_INCLUDED : NRF_UART_PARITY_EXCLUDED;

                    nrf_uart_baudrate_set(_uartInstance[channel].p_reg, _baudRateMap[config.baudRate]);
                    nrf_uart_configure(_uartInstance[channel].p_reg, &nrf_uart_config);
                    nrf_uart_txrx_pins_set(_uartInstance[channel].p_reg,
                                           NRF_GPIO_PIN_MAP(Board::detail::map::uartPins(channel).tx.port,
                                                            Board::detail::map::uartPins(channel).tx.index),
                                           NRF_GPIO_PIN_MAP(Board::detail::map::uartPins(channel).rx.port,
                                                            Board::detail::map::uartPins(channel).rx.index));

                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_TXDRDY);
                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_RXTO);
                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_ERROR);
                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_RXDRDY);
                    nrf_uart_int_enable(_uartInstance[channel].p_reg, NRF_UART_INT_MASK_TXDRDY | NRF_UART_INT_MASK_RXTO);
                    nrf_uart_int_enable(_uartInstance[channel].p_reg, NRF_UART_INT_MASK_RXDRDY | NRF_UART_INT_MASK_ERROR);
                    NRFX_IRQ_PRIORITY_SET(nrfx_get_irq_number((void*)_uartInstance[channel].p_reg), IRQ_PRIORITY_UART);
                    NRFX_IRQ_ENABLE(nrfx_get_irq_number((void*)_uartInstance[channel].p_reg));

                    nrf_uart_task_trigger(_uartInstance[channel].p_reg, NRF_UART_TASK_STARTRX);
                    nrf_uart_enable(_uartInstance[channel].p_reg);

                    return true;
                }
            }    // namespace ll
        }        // namespace UART

        namespace isrHandling
        {
            void uart(uint8_t channel)
            {
                if (nrf_uart_int_enable_check(_uartInstance[channel].p_reg, NRF_UART_INT_MASK_ERROR) && nrf_uart_event_check(_uartInstance[channel].p_reg, NRF_UART_EVENT_ERROR))
                {
                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_ERROR);
                    nrf_uart_int_disable(_uartInstance[channel].p_reg, NRF_UART_INT_MASK_RXDRDY | NRF_UART_INT_MASK_ERROR);
                    nrf_uart_errorsrc_get_and_clear(_uartInstance[channel].p_reg);
                }
                else if (nrf_uart_int_enable_check(_uartInstance[channel].p_reg, NRF_UART_INT_MASK_RXDRDY) && nrf_uart_event_check(_uartInstance[channel].p_reg, NRF_UART_EVENT_RXDRDY))
                {
                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_RXDRDY);
                    Board::detail::UART::storeIncomingData(channel, nrf_uart_rxd_get(_uartInstance[channel].p_reg));
                }

                if (nrf_uart_event_check(_uartInstance[channel].p_reg, NRF_UART_EVENT_TXDRDY))
                {
                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_TXDRDY);

                    size_t  remainingBytes;
                    uint8_t value;

                    if (Board::detail::UART::getNextByteToSend(channel, value, remainingBytes))
                    {
                        nrf_uart_txd_set(_uartInstance[channel].p_reg, value);
                    }
                    else
                    {
                        nrf_uart_task_trigger(_uartInstance[channel].p_reg, NRF_UART_TASK_STOPTX);
                        Board::detail::UART::indicateTxComplete(channel);
                    }
                }

                if (nrf_uart_event_check(_uartInstance[channel].p_reg, NRF_UART_EVENT_RXTO))
                {
                    nrf_uart_event_clear(_uartInstance[channel].p_reg, NRF_UART_EVENT_RXTO);
                    nrf_uart_task_trigger(_uartInstance[channel].p_reg, NRF_UART_TASK_STARTRX);
                }
            }
        }    // namespace isrHandling
    }        // namespace detail
}    // namespace Board

#endif