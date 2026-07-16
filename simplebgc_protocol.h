#ifndef SIMPLEBGC_PROTOCOL_H
#define SIMPLEBGC_PROTOCOL_H

#include <cstdint>
#include <QByteArray>

// SimpleBGC 32-bit Protocol Version 2
// Full packet format after target_id:
// $ | CMD | SIZE | HeaderChecksum | Payload | CRC16

namespace SimpleBGC {

    constexpr uint8_t START_BYTE_V2 = 0x24;   // '$'

    enum CommandID : uint8_t {
        CMD_CONTROL          = 0x67,
        CMD_REALTIME_DATA_3  = 0x23,
        CMD_CONFIRM          = 0x67
    };

    enum ControlMode : uint8_t {
        MODE_ANGLE       = 0,
        MODE_SPEED       = 1,
        MODE_SPEED_ANGLE = 2
    };

    // CRC16 (poly 0x8005, as in official spec)
    inline uint16_t crc16(const uint8_t* data, uint16_t len) {
        uint16_t crc = 0;
        for (uint16_t i = 0; i < len; ++i) {
            crc ^= static_cast<uint16_t>(data[i]) << 8;
            for (uint8_t j = 0; j < 8; ++j) {
                if (crc & 0x8000)
                    crc = (crc << 1) ^ 0x8005;
                else
                    crc <<= 1;
            }
        }
        return crc;
    }

    /**
     * @brief Builds a complete SimpleBGC Protocol v2 packet
     * @param cmd Command ID
     * @param payload Command payload (can be empty)
     * @return Full packet: $ + CMD + SIZE + HCS + Payload + CRC16
     */
    inline QByteArray buildPacket(uint8_t cmd, const QByteArray& payload = QByteArray())
    {
        QByteArray packet;
        uint8_t size = static_cast<uint8_t>(payload.size());

        // Header
        packet.append(static_cast<char>(START_BYTE_V2));
        packet.append(static_cast<char>(cmd));
        packet.append(static_cast<char>(size));

        // Header Checksum = (CMD + SIZE) % 256
        uint8_t headerChecksum = (cmd + size) & 0xFF;
        packet.append(static_cast<char>(headerChecksum));

        // Payload
        if (!payload.isEmpty())
            packet.append(payload);

        // CRC16 over everything except start byte (from CMD to end of payload)
        uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(packet.constData() + 1), packet.size() - 1);
        packet.append(static_cast<char>(crc & 0xFF));
        packet.append(static_cast<char>((crc >> 8) & 0xFF));

        return packet;
    }

} // namespace SimpleBGC

#endif // SIMPLEBGC_PROTOCOL_H