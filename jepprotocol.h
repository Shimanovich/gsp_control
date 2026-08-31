#ifndef JEPPROTOCOL_H
#define JEPPROTOCOL_H

#include <QByteArray>
#include <QString>
#include <utility>
#include <cstdint>

// JSON Exchange Protocol (JEP)
// Packet: Header(4 ASCII) | Size(4 BE) | MD5 hex(32) | Unitime(8 BE ms) | JSON
//
// MD5 = hex( PRIVATE_KEY + Unitime_BE + JSON ), word-byte order as on Jetson
// (boost::uuids::detail::md5 digest words printed as 8-digit hex each).
//
// Header "MDPL" — MediaPlayer control
// Header "CAPT" — Capture / tracking

enum class JEP_HD : quint32 {
    OK   = 0,
    MDPL = 0x4C50444D, // "MDPL" little-endian
    CAPT = 0x54504143  // "CAPT" little-endian
};

class JEPProtocol
{
public:
    static constexpr int HEADER_SIZE   = 4;
    static constexpr int SIZE_SIZE     = 4;
    static constexpr int MD5_SIZE      = 32;
    static constexpr int UNITIME_SIZE  = 8;
    static constexpr int MIN_PACKET_SIZE = HEADER_SIZE + SIZE_SIZE + MD5_SIZE + UNITIME_SIZE;

    static constexpr const char* PRIVATE_KEY = "my_secret_jep_key_2026";

    static QByteArray pack(const QByteArray& jsonData, JEP_HD header);
    static QByteArray pack(const QString& jsonData, JEP_HD header);

    static std::pair<JEP_HD, QByteArray> unpack(const QByteArray& packet);
    static bool validate(const QByteArray& packet);

    static QString headerToString(JEP_HD header);

private:
    static quint64 currentTimeMs();
    static QString calculateMd5Hex(const QByteArray& data);
    static QByteArray buildMd5Data(quint64 unitime, const QByteArray& jsonData);
};

#endif // JEPPROTOCOL_H
