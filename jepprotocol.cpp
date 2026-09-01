#include "jepprotocol.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QtEndian>
#include <cstring>

QString JEPProtocol::headerToString(JEP_HD header)
{
    switch (header) {
    case JEP_HD::OK:   return QStringLiteral("OK  ");
    case JEP_HD::MDPL: return QStringLiteral("MDPL");
    case JEP_HD::CAPT: return QStringLiteral("CAPT");
    default:           return QStringLiteral("    ");
    }
}

quint64 JEPProtocol::currentTimeMs()
{
    return static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
}

QByteArray JEPProtocol::buildMd5Data(quint64 unitime, const QByteArray& jsonData)
{
    QByteArray md5Data;
    md5Data.reserve(static_cast<int>(strlen(PRIVATE_KEY)) + UNITIME_SIZE + jsonData.size());
    md5Data.append(PRIVATE_KEY);
    const quint64 be = qToBigEndian(unitime);
    md5Data.append(reinterpret_cast<const char*>(&be), UNITIME_SIZE);
    md5Data.append(jsonData);
    return md5Data;
}

QString JEPProtocol::calculateMd5Hex(const QByteArray& data)
{
    // 1:1 with JepProtocol.cpp::calculate_md5
    //
    // Qt gives RFC 1321 raw bytes: LE(A) LE(B) LE(C) LE(D).
    // boost::uuids::detail::md5::get_digest on LE + Boost >= 1.71 writes
    // each word via BOOST_UUID_DETAIL_MD5_BYTE_OUT as BE, so the uint32_t
    // view of digest[] is bswap(rfc_word). Then the file does:
    //   sh = digest[3], digest[2], digest[1], digest[0]
    //   hex = sh[15] .. sh[0]
    // Those two transforms cancel → result is the ordinary lowercase MD5 hex.
    //
    // The previous panel version applied only the print-swap to RFC bytes
    // and produced the pre-1.71 Boost hex. Jetson validate() would reject it.

    const QByteArray rfc = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    if (rfc.size() != 16)
        return {};

    quint32 digest[4];
    for (int i = 0; i < 4; ++i) {
        const quint32 rfcWord =
            quint32(quint8(rfc[i * 4])) |
            (quint32(quint8(rfc[i * 4 + 1])) << 8) |
            (quint32(quint8(rfc[i * 4 + 2])) << 16) |
            (quint32(quint8(rfc[i * 4 + 3])) << 24);
        digest[i] = qbswap(rfcWord);
    }

    quint8 sh[16] = {};
    memcpy(sh + 0,  &digest[3], 4);
    memcpy(sh + 4,  &digest[2], 4);
    memcpy(sh + 8,  &digest[1], 4);
    memcpy(sh + 12, &digest[0], 4);

    QString result;
    result.reserve(32);
    for (int i = 0; i < 16; ++i)
        result += QString("%1").arg(sh[15 - i], 2, 16, QLatin1Char('0'));
    return result;
}

QByteArray JEPProtocol::pack(const QByteArray& jsonData, JEP_HD header)
{
    if (jsonData.isEmpty())
        return {};

    const quint64 unitime = currentTimeMs();
    const QString md5Hex = calculateMd5Hex(buildMd5Data(unitime, jsonData));
    if (md5Hex.size() != MD5_SIZE)
        return {};

    const quint32 dataSize = static_cast<quint32>(jsonData.size());

    QByteArray packet;
    packet.reserve(MIN_PACKET_SIZE + jsonData.size());

    const QByteArray hdr = headerToString(header).toLatin1();
    packet.append(hdr.leftJustified(HEADER_SIZE, ' ').left(HEADER_SIZE));

    const quint32 sizeBe = qToBigEndian(dataSize);
    packet.append(reinterpret_cast<const char*>(&sizeBe), SIZE_SIZE);

    packet.append(md5Hex.toLatin1());

    const quint64 timeBe = qToBigEndian(unitime);
    packet.append(reinterpret_cast<const char*>(&timeBe), UNITIME_SIZE);

    packet.append(jsonData);
    return packet;
}

QByteArray JEPProtocol::pack(const QString& jsonData, JEP_HD header)
{
    return pack(jsonData.toUtf8(), header);
}

std::pair<JEP_HD, QByteArray> JEPProtocol::unpack(const QByteArray& packet)
{
    if (packet.size() < MIN_PACKET_SIZE)
        return {JEP_HD::OK, {}};

    const int pos = HEADER_SIZE + SIZE_SIZE + MD5_SIZE + UNITIME_SIZE;
    if (packet.size() <= pos)
        return {JEP_HD::OK, {}};

    quint32 raw = 0;
    memcpy(&raw, packet.constData(), sizeof(raw));
    const JEP_HD hd = static_cast<JEP_HD>(raw);

    return {hd, packet.mid(pos)};
}

bool JEPProtocol::validate(const QByteArray& packet)
{
    if (packet.size() < MIN_PACKET_SIZE)
        return false;

    int pos = HEADER_SIZE + SIZE_SIZE;
    const QByteArray md5FromPacket = packet.mid(pos, MD5_SIZE);
    pos += MD5_SIZE;

    quint64 unitime = 0;
    for (int i = 0; i < UNITIME_SIZE; ++i)
        unitime = (unitime << 8) | static_cast<quint8>(packet[pos + i]);
    pos += UNITIME_SIZE;

    const QByteArray jsonData = packet.mid(pos);
    const QString computed = calculateMd5Hex(buildMd5Data(unitime, jsonData));
    return md5FromPacket == computed.toLatin1();
}
