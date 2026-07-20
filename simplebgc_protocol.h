#ifndef SIMPLEBGC_PROTOCOL_H
#define SIMPLEBGC_PROTOCOL_H

#include <cstdint>
#include <QByteArray>

// SimpleBGC 32-bit Protocol Version 2
// Full packet format after target_id:
// $ | CMD | SIZE | HeaderChecksum | Payload | CRC16

namespace SimpleBGC {

    constexpr uint8_t START_BYTE_V2 = 0x24;
    constexpr uint8_t START_BYTE_V1 = 0x3E;


    enum CommandID : uint8_t {
        CMD_CONTROL          = 67,
        CMD_REALTIME_DATA_3  = 23,
        CMD_CONFIRM          = 67,
        CMD_REALTIME_DATA_4  = 25,
        CMD_MOTORS_ON        = 77
    };

    enum ControlMode : uint8_t {

        CONTROL_MODE_NO                 = 0,
        CONTROL_MODE_SPEED              = 1,
        CONTROL_MODE_ANGLE              = 2,
        CONTROL_MODE_SPEED_ANGLE        = 3,
        CONTROL_MODE_RC                 = 4,
        CONTROL_MODE_ANGLE_REL_FRAME    = 5
    };

    #pragma pack(push, 1)
    typedef struct {
        uint8_t mode[3];
        struct {
            int16_t speed;
            int16_t angle;
        } data[3];
    } cmd_control_ext_t;


    typedef struct {
        uint8_t mode;
        int16_t speedROLL;
        int16_t angleROLL;
        int16_t speedPITCH;
        int16_t anglePITCH;
        int16_t speedYAW;
        int16_t angleYAW;
    } cmd_control_t;
    #pragma pack(pop)


    // RC channels used in the SBGC controller
#define SBGC_RC_NUM_CHANNELS 6 // ROLL, PITCH, YAW, CMD, EXT_ROLL, EXT_PITCH
    // Size of header and checksums
#define SBGC_CMD_NON_PAYLOAD_BYTES 6
    // Max. size of a command after packing to bytes
#define SBGC_CMD_MAX_BYTES 255
    // Max. size of a payload data
#define SBGC_CMD_DATA_SIZE (SBGC_CMD_MAX_BYTES - SBGC_CMD_NON_PAYLOAD_BYTES)

#pragma pack(1)

    // CMD_CONTROL
    typedef struct {
        uint8_t mode;
        int16_t speedROLL;
        int16_t angleROLL;
        int16_t speedPITCH;
        int16_t anglePITCH;
        int16_t speedYAW;
        int16_t angleYAW;
    } SBGC_cmd_control_t;

    typedef struct {
        uint8_t mode[3];
        struct {
            int16_t speed;
            int16_t angle;
        } data[3];
    } SBGC_cmd_control_ext_t;

    // CMD_REALTIME_DATA_3, CMD_REALTIME_DATA_4
    typedef struct {
        struct {
            int16_t acc_data;
            int16_t gyro_data;
        } sensor_data[3];  // ACC and Gyro sensor data (with calibration) for current IMU (see cur_imu field)
        int16_t serial_error_cnt; // counter for communication errors
        int16_t system_error; // system error flags, defined in SBGC_SYS_ERR_XX
        uint8_t reserved1[4];
        int16_t rc_raw_data[SBGC_RC_NUM_CHANNELS]; // RC signal in 1000..2000 range for ROLL, PITCH, YAW, CMD, EXT_ROLL, EXT_PITCH channels
        int16_t imu_angle[3]; // ROLL, PITCH, YAW Euler angles of a camera, 16384/360 degrees
        int16_t frame_imu_angle[3]; // ROLL, PITCH, YAW Euler angles of a frame, if known
        int16_t target_angle[3]; // ROLL, PITCH, YAW target angle
        uint16_t cycle_time_us; // cycle time in us. Normally should be 800us
        uint16_t i2c_error_count; // I2C errors counter
        uint8_t reserved2;
        uint16_t battery_voltage; // units 0.01 V
        uint8_t state_flags1; // bit0: motor ON/OFF state;  bits1..7: reserved
        uint8_t cur_imu; // actually selecteted IMU for monitoring. 1: main IMU, 2: frame IMU
        uint8_t cur_profile; // active profile number starting from 0
        uint8_t motor_power[3]; // actual motor power for ROLL, PITCH, YAW axis, 0..255

        // Fields below are filled only for CMD_REALTIME_DATA_4 command
        int16_t rotor_angle[3]; // relative angle of each motor, 16384/360 degrees
        uint8_t reserved3;
        int16_t balance_error[3]; // error in balance. Ranges from -512 to 512,  0 means perfect balance.
        uint16_t current; // Current that gimbal takes, in mA.
        int16_t magnetometer_data[3]; // magnetometer sensor data (with calibration)
        int8_t  imu_temp_celcius;  // temperature measured by the main IMU sensor, in Celsius
        int8_t  frame_imu_temp_celcius;  // temperature measured by the frame IMU sensor, in Celsius
        uint8_t reserved4[38];
    } SBGC_cmd_realtime_data_t;


    // tupes spec
    typedef struct {
        uint8_t start;
        uint8_t id;
        uint8_t size;
        uint8_t hcs;
        union
        {
            SBGC_cmd_control_t  		cmd_control;
            SBGC_cmd_control_ext_t		cmd_control_ext;
            SBGC_cmd_realtime_data_t    rtdata;
            uint8_t payloadData[SBGC_CMD_DATA_SIZE];
        };

        uint8_t CRCsumm[2];
    } SerialCommand_t;

#pragma pop(0)


    inline void crc16_update(uint16_t length,const uint8_t *data, uint8_t crc[2]) {
        uint16_t counter;
        uint16_t polynom = 0x8005;
        uint16_t crc_register = (uint16_t)crc[0] | ((uint16_t)crc[1] << 8);
        uint8_t shift_register;
        uint8_t data_bit, crc_bit;
        for (counter = 0; counter < length; counter++) {
            for (shift_register = 0x01; shift_register > 0x00; shift_register <<= 1) {
                data_bit = (data[counter] & shift_register) ? 1 : 0;
                crc_bit = crc_register >> 15;
                crc_register <<= 1;
                if (data_bit != crc_bit) crc_register ^= polynom;
            }
        }
        crc[0] = crc_register;
        crc[1] = (crc_register >> 8);
    }
    inline void crc16_calculate(uint16_t length,const uint8_t *data, uint8_t * crc) {
        crc[0] = 0; crc[1] = 0;
        crc16_update(length, data, crc);
    }


    inline uint8_t sumBytesFromSecond(const QByteArray &ba)
    {
        uint8_t sum = 0;
        for (int i = 0; i < ba.size(); ++i) {
            sum += static_cast<uint8_t>(ba.at(i));
        }
        return sum;
    }

    /**
     * @brief Builds a complete SimpleBGC Protocol v2 packet
     * @param cmd Command ID
     * @param payload Command payload (can be empty)
     * @return Full packet: $ + CMD + SIZE + HCS + Payload + CRC16
     */


    inline QByteArray buildPacket(uint8_t cmd, const QByteArray& payload = QByteArray())
    {
        // QByteArray packet;
        //  uint8_t size = static_cast<uint8_t>(payload.size());

        //  // Header
        //  packet.append(static_cast<char>(START_BYTE_V2));
        //  packet.append(static_cast<char>(cmd));
        //  packet.append(static_cast<char>(size));

        //  // Header Checksum = (CMD + SIZE) % 256
        //  uint8_t headerChecksum = (cmd + (uint8_t)size) & 0xFF;
        //  packet.append(static_cast<char>(headerChecksum));

        //  // Payload
        //  if (!payload.isEmpty())
        //      packet.append(payload);

        // uint8_t crc[2];
        // crc16_calculate(packet.size() - 1,reinterpret_cast<const uint8_t*>(packet.constData() + 1), crc);
        // packet.append(static_cast<char>(crc[0]));
        // packet.append(static_cast<char>(crc[1]));

        // return packet;

         // V 1.0

         QByteArray packet;
         uint8_t size = static_cast<uint8_t>(payload.size());

         packet.append(static_cast<char>(START_BYTE_V1));
         packet.append(static_cast<char>(cmd));
         packet.append(static_cast<char>(size));

         // Header Checksum = (CMD + SIZE) % 256
         uint8_t headerChecksum = (cmd + (uint8_t)size) & 0xFF;
         packet.append(static_cast<char>(headerChecksum));

         // Payload
         if (!payload.isEmpty())
         {
            packet.append(payload);
            uint8_t sum = sumBytesFromSecond(payload);
            packet.append(sum);
         }
         else
         {
             packet.append(static_cast<char>(0));
         }





         return packet;
    }





} // namespace SimpleBGC

#endif // SIMPLEBGC_PROTOCOL_H