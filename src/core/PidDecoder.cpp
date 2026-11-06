#include "PidDecoder.h"
#include <QStringList>

double PidDecoder::decodeRpm(uint8_t a, uint8_t b)
{
    return ((static_cast<double>(a) * 256.0) + static_cast<double>(b)) / 4.0;
}

double PidDecoder::decodeSpeed(uint8_t a)
{
    return static_cast<double>(a);
}

double PidDecoder::decodeCoolantTemp(uint8_t a)
{
    return static_cast<double>(a) - 40.0;
}

double PidDecoder::decodeIntakeAirTemp(uint8_t a)
{
    return static_cast<double>(a) - 40.0;
}

double PidDecoder::decodeMaf(uint8_t a, uint8_t b)
{
    return ((static_cast<double>(a) * 256.0) + static_cast<double>(b)) / 100.0;
}

double PidDecoder::decodeThrottle(uint8_t a)
{
    return (static_cast<double>(a) * 100.0) / 255.0;
}

double PidDecoder::decodeControlModuleVoltage(uint8_t a, uint8_t b)
{
    return ((static_cast<double>(a) * 256.0) + static_cast<double>(b)) / 1000.0;
}

double PidDecoder::decodeToyotaTransTemp(uint8_t a)
{
    return static_cast<double>(a) - 40.0;
}

QByteArray PidDecoder::extractDataBytes(const QString& rawResponse, const QString& serviceHex, const QString& pidHex)
{
    QString cleanService = serviceHex.trimmed().toUpper();
    QString cleanPid = pidHex.trimmed().toUpper();
    QStringList pidTokens = cleanPid.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    const QStringList lines = rawResponse.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString cleanLine = line.trimmed();
        if (cleanLine.isEmpty() || cleanLine.contains(QLatin1Char('>'))) continue;

        QStringList tokens = cleanLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int i = 0; i < tokens.size(); ++i) {
            if (tokens.at(i).toUpper() == cleanService) {
                // Verificar si los siguientes tokens corresponden al PID
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

