#include "ObdProtocol.h"
#include "PidDecoder.h"
#include "SafetyGuard.h"
#include <QRegularExpression>
#include <QDebug>

ObdProtocol::ObdProtocol(IObdInterface* interface,
                         DtcManager* dtcManager,
                         ObdbJsonLoader* obdbLoader,
                         DbcParser* dbcParser,
                         QObject* parent)
    : QObject(parent)
    , m_interface(interface)
    , m_dtcManager(dtcManager)
    , m_obdbLoader(obdbLoader)
    , m_dbcParser(dbcParser)
{
}

QString ObdProtocol::detectActiveProtocol()
{
    if (!m_interface) return QStringLiteral("Interfaz no disponible");

    // 1. Configurar detección automática
    m_interface->sendRawCommand(QStringLiteral("AT SP 0"));

    // 2. Transmitir handshake funcional Mode 01 PID 00 para forzar negociación con el bus
    m_interface->sendRawCommand(QStringLiteral("01 00"));

    // 3. Consultar nombre del protocolo detectado
    QString dpResp = m_interface->sendRawCommand(QStringLiteral("AT DP")).trimmed();
    // Limpiar prompt residual
    dpResp.remove(QLatin1Char('>'));
    dpResp.remove(QLatin1Char('\r'));
    dpResp.remove(QLatin1Char('\n'));

    // 4. Consultar número de protocolo
    QString dpnResp = m_interface->sendRawCommand(QStringLiteral("AT DPN")).trimmed();
    dpnResp.remove(QLatin1Char('>'));
    dpnResp.remove(QLatin1Char('\r'));
    dpnResp.remove(QLatin1Char('\n'));

    QString result;
    if (!dpResp.isEmpty() && !dpResp.contains(QStringLiteral("?")) && !dpResp.contains(QStringLiteral("ERROR"))) {
        result = dpResp;
        if (!dpnResp.isEmpty() && !dpnResp.contains(QStringLiteral("?"))) {
            result += QStringLiteral(" [Protocolo %1]").arg(dpnResp);
        }
    } else {
        result = QStringLiteral("Protocolo no detectado (Requiere conexión al vehículo)");
    }

    m_activeProtocolDescription = result;
    return result;
}

std::optional<double> ObdProtocol::querySignal(const QString& signalId)
{
    if (!m_interface) return std::nullopt;

    // 1. Buscar en el catálogo cargado de OBDb
    if (m_obdbLoader && m_obdbLoader->isLoaded()) {
        const ObdbSignal* sig = m_obdbLoader->findSignal(signalId);
        const ObdbCommand* cmd = m_obdbLoader->findCommandForSignal(signalId);

        if (sig && cmd) {
            // Configurar direccionamiento específico si la señal lo define
            if (!cmd->hdr.isEmpty()) {
                setAddressing(cmd->hdr, cmd->rax);
            }

            QString resp = m_interface->sendRawCommand(cmd->fullCmdHex);
            auto val = PidDecoder::decodeObdbSignal(resp, cmd->service, cmd->pidHex, sig->fmt);

            // Restaurar direccionamiento funcional estándar si se modificó
            if (!cmd->hdr.isEmpty() && cmd->hdr != QStringLiteral("7DF")) {
                setFunctionalAddressing();
            }

            if (val.has_value()) {
                return val;
            }
        }
    }

    return std::nullopt;
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
    setFunctionalAddressing();
    QString resp = m_interface->sendRawCommand(QStringLiteral("03"));
    return m_dtcManager->parseDtcResponse(resp);
}

QList<DtcInfo> ObdProtocol::readPendingDtcs()
{
    if (!m_interface || !m_dtcManager) return {};
    setFunctionalAddressing();
    QString resp = m_interface->sendRawCommand(QStringLiteral("07"));
    return m_dtcManager->parseDtcResponse(resp);
}

bool ObdProtocol::clearDtcs(bool userConfirmed)
{
    if (!userConfirmed || !m_interface) return false;

    SafetyGuard::setUserConfirmedClearDtc(true);
    QString resp = m_interface->sendRawCommand(QStringLiteral("04"));
    SafetyGuard::setUserConfirmedClearDtc(false);

    return resp.contains(QStringLiteral("44")) || resp.contains(QStringLiteral("OK"));
}

QString ObdProtocol::readVin()
{
    if (!m_interface) return QString();
    setFunctionalAddressing();
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

    if (payload.size() >= 17) {
        return QString::fromLatin1(payload.left(17)).trimmed();
    }

    return QString();
}

bool ObdProtocol::setFunctionalAddressing()
{
    if (!m_interface) return false;
    m_interface->sendRawCommand(QStringLiteral("AT SH 7DF"));
    m_interface->sendRawCommand(QStringLiteral("AT CRA"));
    return true;
}

bool ObdProtocol::setAddressing(const QString& header, const QString& cra)
{
    if (!m_interface || header.isEmpty()) return false;
    m_interface->sendRawCommand(QStringLiteral("AT SH ") + header.trimmed().toUpper());
    if (!cra.isEmpty()) {
        m_interface->sendRawCommand(QStringLiteral("AT CRA ") + cra.trimmed().toUpper());
    } else {
        m_interface->sendRawCommand(QStringLiteral("AT CRA"));
    }
    return true;
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

        // Si la línea incluye header CAN (ej. 7E8 o 18DAF110), omitir el primer token
        int tokenOffset = 0;
        if (tokens.first().length() == 3 || tokens.first().length() == 8) {
            tokenOffset = 1;
        }

        if (tokenOffset >= tokens.size()) continue;

        // Caso A: Línea de longitud hexadecimal de 3 dígitos propia de ELM327 CAF1 (ej. "014" o "7E8 014")
        if (tokens.size() - tokenOffset == 1 && tokens.at(tokenOffset).length() == 3) {
            bool okLen = false;
            int len = tokens.at(tokenOffset).toInt(&okLen, 16);
            if (okLen) {
                expectedLength = len;
                continue;
            }
        }

        // Caso B: Líneas segmentadas numeradas de ELM327 CAF1 (ej. "0: 49 02 ...", "1: ...")
        QString firstTok = tokens.at(tokenOffset);
        if (firstTok.endsWith(QLatin1Char(':'))) {
            for (int k = tokenOffset + 1; k < tokens.size(); ++k) {
                bool ok = false;
                uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                if (ok) fullPayload.append(static_cast<char>(val));
            }
            continue;
        }
        if (firstTok.length() == 1 && tokenOffset + 1 < tokens.size() && tokens.at(tokenOffset + 1) == QStringLiteral(":")) {
            for (int k = tokenOffset + 2; k < tokens.size(); ++k) {
                bool ok = false;
                uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                if (ok) fullPayload.append(static_cast<char>(val));
            }
            continue;
        }

        bool okPci = false;
        uint8_t pciByte = static_cast<uint8_t>(tokens.at(tokenOffset).toUInt(&okPci, 16));
        if (!okPci) continue;

        uint8_t frameType = (pciByte >> 4) & 0x0F;

        // Single Frame ISO-TP (0x0N)
        if (frameType == 0) {
            int length = pciByte & 0x0F;
            for (int k = tokenOffset + 1; k < tokens.size() && (k - (tokenOffset + 1)) < length; ++k) {
                bool ok = false;
                uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                if (ok) fullPayload.append(static_cast<char>(val));
            }
            break;
        }
        // First Frame ISO-TP (0x1N)
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
        // Consecutive Frame ISO-TP (0x2N)
        else if (frameType == 2) {
            for (int k = tokenOffset + 1; k < tokens.size(); ++k) {
                if (expectedLength > 0 && fullPayload.size() >= expectedLength) break;
                bool ok = false;
                uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                if (ok) fullPayload.append(static_cast<char>(val));
            }
        }
        // Caso C: Trama sin byte PCI (pciByte es directamente un Service ID de respuesta positiva >= 0x40)
        else if (pciByte >= 0x40 && pciByte <= 0x7F) {
            for (int k = tokenOffset; k < tokens.size(); ++k) {
                bool ok = false;
                uint8_t val = static_cast<uint8_t>(tokens.at(k).toUInt(&ok, 16));
                if (ok) fullPayload.append(static_cast<char>(val));
            }
            break;
        }
    }

    if (expectedLength > 0 && fullPayload.size() > expectedLength) {
        fullPayload.truncate(expectedLength);
    }

    return fullPayload;
}
