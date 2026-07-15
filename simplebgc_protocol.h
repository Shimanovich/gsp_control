#ifndef SIMPLEBGC_PROTOCOL_H
#define SIMPLEBGC_PROTOCOL_H

#include <cstdint>

// SimpleBGC Protocol Version 2 constants
namespace SimpleBGC {

    constexpr uint8_t START_BYTE_V2 = 0x24;  // '$'

    // Command IDs (from specification)
    enum CommandID : uint8_t {
        CMD_CONTROL              = 0x67,
        CMD_REALTIME_DATA_3      = 0x23,
        CMD_CONFIRM              = 0x67,   // same as control for confirmation in some cases
        CMD_ERROR                = 0xFF
    };

    // Control modes for CMD_CONTROL
    enum ControlMode : uint8_t {
        MODE_ANGLE   = 0,   // position control
        MODE_SPEED   = 1,   // speed control
        MODE_SPEED_ANGLE = 2
    };

    // Helper to calculate CRC16 (polynomial 0x8005) for Protocol v2
    inline uint16_t crc16(const uint8_t* data, uint16_t len) {
        uint16_t crc = 0;
        for (uint16_t i = 0; i < len; ++i) {
            crc ^= data[i] << 8;
            for (uint8_t j = 0; j < 8; ++j) {
                if (crc & 0x8000)
                    crc = (crc << 1) ^ 0x8005;
                else
                    crc <<= 1;
            }
        }
        return crc;
    }

} // namespace SimpleBGC

#endif // SIMPLEBGC_PROTOCOL_H