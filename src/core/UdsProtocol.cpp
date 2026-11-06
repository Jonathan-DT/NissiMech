#include "UdsProtocol.h"
#include "PidDecoder.h"
#include <QDebug>

UdsProtocol::UdsProtocol(IObdInterface* interface,
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

bool UdsProtocol::selectPhysicalModule(const QString& txHeader, const QString& rxCra)
{
    if (!m_interface || txHeader.isEmpty()) return false;

    m_interface->sendRawCommand(QStringLiteral("AT SH ") + txHeader.trimmed().toUpper());
    if (!rxCra.isEmpty()) {
        m_interface->sendRawCommand(QStringLiteral("AT CRA ") + rxCra.trimmed().toUpper());
    }
    return true;
}

bool UdsProtocol::restoreFunctionalAddressing()
{
    if (!m_interface) return false;
    m_interface->sendRawCommand(QStringLiteral("AT CRA"));
    m_interface->sendRawCommand(QStringLiteral("AT SH 7DF"));
    return true;
}

QList<DtcInfo> UdsProtocol::readModuleDtcs(const QString& txHeader, const QString& rxCra)
{
    if (!m_interface || !m_dtcManager || txHeader.isEmpty()) return {};

    selectPhysicalModule(txHeader, rxCra);

    // Servicio UDS 0x19 0x02 con máscara 0xFF (ReadDTCInformationByStatusMask)
    QString resp = m_interface->sendRawCommand(QStringLiteral("19 02 FF"));
    QList<DtcInfo> dtcs;

    if (resp.contains(QStringLiteral("59 02"))) {
        dtcs = m_dtcManager->parseDtcResponse(resp);
    } else if (resp.contains(QStringLiteral("NO DATA")) || resp.contains(QStringLiteral("?"))) {
        // Fallback a modo OBD 03 físico si el módulo no soporta UDS 19 02
        QString fallbackResp = m_interface->sendRawCommand(QStringLiteral("03"));
        dtcs = m_dtcManager->parseDtcResponse(fallbackResp);
    }

    restoreFunctionalAddressing();
    return dtcs;
}

QByteArray UdsProtocol::readDataByIdentifier(const QString& txHeader, uint16_t did, const QString& rxCra)
{
    if (!m_interface || txHeader.isEmpty()) return QByteArray();

    selectPhysicalModule(txHeader, rxCra);

    uint8_t didHigh = static_cast<uint8_t>((did >> 8) & 0xFF);
    uint8_t didLow = static_cast<uint8_t>(did & 0xFF);
    QString cmd = QStringLiteral("22 %1 %2")
                      .arg(didHigh, 2, 16, QLatin1Char('0'))
                      .arg(didLow, 2, 16, QLatin1Char('0'))
                      .toUpper();
    QString serviceHex = QStringLiteral("62");
    QString didHex = QStringLiteral("%1 %2")
                         .arg(didHigh, 2, 16, QLatin1Char('0'))
                         .arg(didLow, 2, 16, QLatin1Char('0'))
                         .toUpper();

    QString resp = m_interface->sendRawCommand(cmd);
    QByteArray data = PidDecoder::extractDataBytes(resp, serviceHex, didHex);

    restoreFunctionalAddressing();
    return data;
}

std::optional<double> UdsProtocol::querySignal(const QString& signalId)
{
    if (!m_interface || !m_obdbLoader || !m_obdbLoader->isLoaded()) {
        return std::nullopt;
    }

    const ObdbSignal* sig = m_obdbLoader->findSignal(signalId);
    const ObdbCommand* cmd = m_obdbLoader->findCommandForSignal(signalId);

    if (sig && cmd) {
        if (!cmd->hdr.isEmpty()) {
            selectPhysicalModule(cmd->hdr, cmd->rax);
        }

        QString resp = m_interface->sendRawCommand(cmd->fullCmdHex);
        auto val = PidDecoder::decodeObdbSignal(resp, cmd->service, cmd->pidHex, sig->fmt);

        restoreFunctionalAddressing();
        return val;
    }

    return std::nullopt;
}
