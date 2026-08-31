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
    // Must match Jetson JepProtocol.cpp:
    // boost digest words printed as 8-digit hex (MSB first within each word).
    const QByteArray digest = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    if (digest.size() != 16)
        return QString();

    QString result;
    result.resize(32);
    int out = 0;
    for (int w = 0; w < 4; ++w) {
        for (int b = 3; b >= 0; --b) {
            const quint8 v = static_cast<quint8>(digest[w * 4 + b]);
            result[out++] = QChar(QLatin1Char("0123456789abcdef"[v >> 4]));
            result[out++] = QChar(QLatin1Char("0123456789abcdef"[v & 0x0F]));
        }
    }
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
