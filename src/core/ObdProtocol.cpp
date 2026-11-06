#include "ObdProtocol.h"
#include "PidDecoder.h"
#include "SafetyGuard.h"
#include <QRegularExpression>
#include <QDebug>

ObdProtocol::ObdProtocol(IObdInterface* interface, DtcManager* dtcManager, QObject* parent)
    : QObject(parent)
    , m_interface(interface)
    , m_dtcManager(dtcManager)
{
}

bool ObdProtocol::setFunctionalAddressing()
{
    if (!m_interface) return false;
    m_interface->sendRawCommand(QStringLiteral("AT SH 7DF"));
    m_interface->sendRawCommand(QStringLiteral("AT CRA"));
    return true;
}

bool ObdProtocol::setPhysicalEngineAddressing()
{
    if (!m_interface) return false;
    m_interface->sendRawCommand(QStringLiteral("AT SH 7E0"));
    m_interface->sendRawCommand(QStringLiteral("AT CRA 7E8"));
    return true;
}

double ObdProtocol::readRpm()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("01 0C"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("41"), QStringLiteral("0C"));
    if (data.size() >= 2) {
        return PidDecoder::decodeRpm(static_cast<uint8_t>(data.at(0)), static_cast<uint8_t>(data.at(1)));
    }
    return 0.0;
}

double ObdProtocol::readSpeed()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("01 0D"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("41"), QStringLiteral("0D"));
    if (data.size() >= 1) {
        return PidDecoder::decodeSpeed(static_cast<uint8_t>(data.at(0)));
    }
    return 0.0;
}

double ObdProtocol::readCoolantTemp()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("01 05"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("41"), QStringLiteral("05"));
    if (data.size() >= 1) {
        return PidDecoder::decodeCoolantTemp(static_cast<uint8_t>(data.at(0)));
    }
    return 0.0;
}

double ObdProtocol::readIntakeAirTemp()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("01 0F"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("41"), QStringLiteral("0F"));
    if (data.size() >= 1) {
        return PidDecoder::decodeIntakeAirTemp(static_cast<uint8_t>(data.at(0)));
    }
    return 0.0;
}

double ObdProtocol::readMaf()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("01 10"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("41"), QStringLiteral("10"));
    if (data.size() >= 2) {
        return PidDecoder::decodeMaf(static_cast<uint8_t>(data.at(0)), static_cast<uint8_t>(data.at(1)));
    }
    return 0.0;
}

double ObdProtocol::readThrottle()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("01 11"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("41"), QStringLiteral("11"));
    if (data.size() >= 1) {
        return PidDecoder::decodeThrottle(static_cast<uint8_t>(data.at(0)));
    }
    return 0.0;
}

double ObdProtocol::readControlModuleVoltage()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("01 42"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("41"), QStringLiteral("42"));
    if (data.size() >= 2) {
        return PidDecoder::decodeControlModuleVoltage(static_cast<uint8_t>(data.at(0)), static_cast<uint8_t>(data.at(1)));
    }
    return 0.0;
}

double ObdProtocol::readBatteryVoltage()
{
    if (!m_interface) return 0.0;
    QString resp = m_interface->sendRawCommand(QStringLiteral("AT RV"));
    static const QRegularExpression voltRegex(QStringLiteral("([0-9]+\\.?[0-9]*)V"), QRegularExpression::CaseInsensitiveOption);
    auto match = voltRegex.match(resp);
    if (match.hasMatch()) {
        return match.captured(1).toDouble();
    }
    return 0.0;
}

QList<DtcInfo> ObdProtocol::readStoredDtcs()
{
    if (!m_interface || !m_dtcManager) return {};
    QString resp = m_interface->sendRawCommand(QStringLiteral("03"));
    return m_dtcManager->parseDtcResponse(resp);
}

QList<DtcInfo> ObdProtocol::readPendingDtcs()
{
    if (!m_interface || !m_dtcManager) return {};
    QString resp = m_interface->sendRawCommand(QStringLiteral("07"));
    return m_dtcManager->parseDtcResponse(resp);
}

bool ObdProtocol::clearDtcs(bool userConfirmed)
{
    if (!userConfirmed) return false;
    if (!m_interface) return false;

    SafetyGuard::setUserConfirmedClearDtc(true);
    QString resp = m_interface->sendRawCommand(QStringLiteral("04"));
    SafetyGuard::setUserConfirmedClearDtc(false);

    return resp.contains(QStringLiteral("44")) || resp.contains(QStringLiteral("OK"));
}

QString ObdProtocol::readVin()
{
    if (!m_interface) return QString();
    QString resp = m_interface->sendRawCommand(QStringLiteral("09 02"));
    QByteArray payload = reassembleIsoTpPayload(resp);

    if (payload.size() >= 3) {
        // En respuesta a 09 02: 49 02 [numItems] [17 bytes VIN]
        if (static_cast<uint8_t>(payload.at(0)) == 0x49 && static_cast<uint8_t>(payload.at(1)) == 0x02) {
            int vinStart = 3; // Omitir 49 02 01
            QByteArray vinBytes = payload.mid(vinStart, 17);
            return QString::fromLatin1(vinBytes).trimmed();
        }
    }

    // Si los datos vinieron directamente en ASCII
    if (payload.size() >= 17) {
        return QString::fromLatin1(payload.left(17)).trimmed();
    }

    return QString();
}

QByteArray ObdProtocol::reassembleIsoTpPayload(const QString& rawResponse)
{
    QByteArray fullPayload;
    int expectedLength = -1;

    const QStringList lines = rawResponse.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString cleanLine = line.trimmed();
        if (cleanLine.isEmpty() || cleanLine.contains(QLatin1Char('>'))) continue;

        QStringList tokens = cleanLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.isEmpty()) continue;

        // Si la línea incluye header CAN (ej. 7E8), omitir el primer token
        int tokenOffset = 0;
        if (tokens.first().length() == 3 || tokens.first().length() == 8) {
            tokenOffset = 1;
        }

        if (tokenOffset >= tokens.size()) continue;

        bool okPci = false;
        uint8_t pciByte = static_cast<uint8_t>(tokens.at(tokenOffset).toUInt(&okPci, 16));
        if (!okPci) continue;

        uint8_t frameType = (pciByte >> 4) & 0x0F;

        // Single Frame (0x0N)
        if (frameType == 0) {
            int length = pciByte & 0x0F;
            for (int k = tokenOffset + 1; k < tokens.size() && (k - (tokenOffset + 1)) < length; ++k) {
                bool ok = false;
                uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                if (ok) fullPayload.append(static_cast<char>(val));
            }
            break;
        }
        // First Frame (0x1N)
        else if (frameType == 1) {
            if (tokenOffset + 1 < tokens.size()) {
                bool okLen = false;
                uint8_t lenLow = static_cast<uint8_t>(tokens.at(tokenOffset + 1).toUInt(&okLen, 16));
                expectedLength = ((pciByte & 0x0F) << 8) | lenLow;
                for (int k = tokenOffset + 2; k < tokens.size(); ++k) {
                    bool ok = false;
                    uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                    if (ok) fullPayload.append(static_cast<char>(val));
                }
            }
        }
        // Consecutive Frame (0x2N)
        else if (frameType == 2) {
            for (int k = tokenOffset + 1; k < tokens.size(); ++k) {
                if (expectedLength > 0 && fullPayload.size() >= expectedLength) break;
                bool ok = false;
                uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                if (ok) fullPayload.append(static_cast<char>(val));
            }
        }
    }

    return fullPayload;
}

