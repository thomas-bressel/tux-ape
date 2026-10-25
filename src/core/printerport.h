#pragma once

#include <cstdint>

namespace tuxape {

// The printer's port seen from the printer: told each character as the
// strobe falls, asked whether it is busy as the PPI's port B is read. The
// time is the machine's, in microseconds. `data` has the eight lines of the
// cable: a 464 or 6128 drives seven, the eighth staying 0; a Plus drives
// the eighth with bit 3 of the CRTC's register 12.
class PrinterPort {
public:
    virtual ~PrinterPort() = default;
    virtual void receive(uint8_t data, uint64_t microsecond) = 0;
    virtual bool busy(uint64_t microsecond) = 0;
};

}  // namespace tuxape
