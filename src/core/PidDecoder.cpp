#include "PidDecoder.h"
#include "ObdProtocol.h"
#include <QStringList>

std::optional<double> PidDecoder::decodeObdbSignal(const QString& rawResponse,
                                                  const QString& serviceHex,
                                                  const QString& pidHex,
                                                  const ObdbSignalFormat& fmt)
{
    QByteArray dataBytes = extractDataBytes(rawResponse, serviceHex, pidHex);
    if (dataBytes.isEmpty()) {
        return std::nullopt;
    }

    return SignalExtractor::extractObdbValue(dataBytes, fmt);
}

QByteArray PidDecoder::extractDataBytes(const QString& rawResponse, const QString& serviceHex, const QString& pidHex)
{
    QString cleanService = serviceHex.trimmed().toUpper();
    QString cleanPid = pidHex.trimmed().toUpper();
    QStringList pidTokens = cleanPid.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    bool okService = false;
    uint8_t reqService = static_cast<uint8_t>(cleanService.toUInt(&okService, 16));
    uint8_t respService = (reqService < 0x40) ? static_cast<uint8_t>(reqService + 0x40) : reqService;
    QString respServiceHex = QString::asprintf("%02X", respService);

    // 1. Primero intentar reensamblado ISO-TP completo para tramas multiframe
    QByteArray isoTpPayload = ObdProtocol::reassembleIsoTpPayload(rawResponse);
    if (!isoTpPayload.isEmpty()) {
        const auto* udata = reinterpret_cast<const uint8_t*>(isoTpPayload.constData());
        int payloadLen = isoTpPayload.size();

        if (okService) {
            for (int i = 0; i < payloadLen; ++i) {
                if (udata[i] == reqService || udata[i] == respService) {
                    bool match = true;
                    int cur = i + 1;
                    for (const QString& tok : pidTokens) {
                        bool okTok = false;
                        uint8_t expTok = static_cast<uint8_t>(tok.toUInt(&okTok, 16));
                        if (!okTok || cur >= payloadLen || udata[cur] != expTok) {
                            match = false;
                            break;
                        }
                        cur++;
                    }

                    if (match) {
                        return isoTpPayload.mid(cur);
                    }
                }
            }
        }
    }

    // 2. Fallback: Parseo línea por línea si el reensamblado directo no encontró el servicio
    const QStringList lines = rawResponse.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString cleanLine = line.trimmed();
        if (cleanLine.isEmpty() || cleanLine.contains(QLatin1Char('>'))) continue;

        QStringList tokens = cleanLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int i = 0; i < tokens.size(); ++i) {
            QString tokUpper = tokens.at(i).toUpper();
            if (tokUpper == cleanService || tokUpper == respServiceHex) {
                bool pidMatch = true;
                int currentIdx = i + 1;
                for (const QString& expectedPart : pidTokens) {
                    if (currentIdx >= tokens.size() || tokens.at(currentIdx).toUpper() != expectedPart) {
                        pidMatch = false;
                        break;
                    }
                    currentIdx++;
                }

                if (pidMatch) {
                    QByteArray bytes;
                    for (int k = currentIdx; k < tokens.size(); ++k) {
                        bool ok = false;
                        uint byteVal = tokens.at(k).toUInt(&ok, 16);
                        if (ok) {
                            bytes.append(static_cast<char>(byteVal));
                        }
                    }
                    return bytes;
                }
            }
        }
    }

    return QByteArray();
}
