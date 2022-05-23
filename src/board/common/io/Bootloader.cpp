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

#ifdef BTLDR_BUTTON_SUPPORTED

#include "board/Board.h"
#include "board/Internal.h"
#include "core/src/util/Util.h"
#include <Target.h>

namespace Board::detail::IO::bootloader
{
    void init()
    {
#ifdef BTLDR_BUTTON_AH
        CORE_MCU_IO_INIT(BTLDR_BUTTON_PORT, BTLDR_BUTTON_PIN, core::mcu::io::pinMode_t::INPUT, core::mcu::io::pullMode_t::DOWN);
#else
        CORE_MCU_IO_INIT(BTLDR_BUTTON_PORT, BTLDR_BUTTON_PIN, core::mcu::io::pinMode_t::INPUT, core::mcu::io::pullMode_t::UP);
#endif
    }
}    // namespace Board::detail::IO::bootloader

#endif