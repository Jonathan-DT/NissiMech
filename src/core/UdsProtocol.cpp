#include "UdsProtocol.h"
#include "PidDecoder.h"
#include <QDebug>

UdsProtocol::UdsProtocol(IObdInterface* interface, DtcManager* dtcManager, QObject* parent)
    : QObject(parent)
    , m_interface(interface)
    , m_dtcManager(dtcManager)
{
}

bool UdsProtocol::selectModule(TargetModule module)
{
    if (!m_interface) return false;

    switch (module) {
    case TargetModule::Engine_ECM:
        m_interface->sendRawCommand(QStringLiteral("AT SH 7E0"));
        m_interface->sendRawCommand(QStringLiteral("AT CRA 7E8"));
        break;
    case TargetModule::Transmission_TCM:
        m_interface->sendRawCommand(QStringLiteral("AT SH 7E1"));
        m_interface->sendRawCommand(QStringLiteral("AT CRA 7E9"));
        break;
    case TargetModule::Brakes_ABS_ISO:
        m_interface->sendRawCommand(QStringLiteral("AT SH 7E2"));
        m_interface->sendRawCommand(QStringLiteral("AT CRA 7EA"));
        break;
    case TargetModule::Brakes_ABS_Toyota:
        m_interface->sendRawCommand(QStringLiteral("AT SH 7B0"));
        m_interface->sendRawCommand(QStringLiteral("AT CRA 7B8"));
        break;
    case TargetModule::Brakes_ABS_Ford_MSCAN:
        m_interface->sendRawCommand(QStringLiteral("AT SH 760"));
        m_interface->sendRawCommand(QStringLiteral("AT CRA 768"));
        break;
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

QList<DtcInfo> UdsProtocol::readModuleDtcs(TargetModule module)
{
    if (!m_interface || !m_dtcManager) return {};

    selectModule(module);

    // Enviar servicio UDS 0x19 0x02 con máscara 0xFF (ReadDTCInformationByStatusMask)
    QString resp = m_interface->sendRawCommand(QStringLiteral("19 02 FF"));
    QList<DtcInfo> dtcs;

    if (resp.contains(QStringLiteral("59 02"))) {
        dtcs = m_dtcManager->parseDtcResponse(resp);
    } else if (resp.contains(QStringLiteral("NO DATA")) || resp.contains(QStringLiteral("?"))) {
        // Fallback a modo OBD 03 físico en caso de ECUs mixtas
        QString fallbackResp = m_interface->sendRawCommand(QStringLiteral("03"));
        dtcs = m_dtcManager->parseDtcResponse(fallbackResp);
    }

    restoreFunctionalAddressing();
    return dtcs;
}

QByteArray UdsProtocol::readDataByIdentifier(TargetModule module, uint16_t did)
{
    if (!m_interface) return QByteArray();

    selectModule(module);

    QString cmd;
    QString serviceHex;
    QString didHex;

    if (module == TargetModule::Brakes_ABS_Toyota && did <= 0xFF) {
        // En ECUs Toyota K-Line/CAN antiguas se utiliza el servicio 0x21
        cmd = QStringLiteral("21 %1").arg(did, 2, 16, QLatin1Char('0')).toUpper();
        serviceHex = QStringLiteral("61");
        didHex = QStringLiteral("%1").arg(did, 2, 16, QLatin1Char('0')).toUpper();
    } else {
        // Estándar UDS ISO 14229 Servicio 0x22
        uint8_t didHigh = static_cast<uint8_t>((did >> 8) & 0xFF);
        uint8_t didLow = static_cast<uint8_t>(did & 0xFF);
        cmd = QStringLiteral("22 %1 %2")
                  .arg(didHigh, 2, 16, QLatin1Char('0'))
                  .arg(didLow, 2, 16, QLatin1Char('0'))
                  .toUpper();
        serviceHex = QStringLiteral("62");
        didHex = QStringLiteral("%1 %2")
                     .arg(didHigh, 2, 16, QLatin1Char('0'))
                     .arg(didLow, 2, 16, QLatin1Char('0'))
                     .toUpper();
    }

    QString resp = m_interface->sendRawCommand(cmd);
    QByteArray data = PidDecoder::extractDataBytes(resp, serviceHex, didHex);

    restoreFunctionalAddressing();
    return data;
}

double UdsProtocol::readToyotaTransmissionFluidTemp()
{
    if (!m_interface) return 0.0;

    selectModule(TargetModule::Transmission_TCM);

    // Intentar PID Toyota 21 82
    QString resp = m_interface->sendRawCommand(QStringLiteral("21 82"));
    QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("61"), QStringLiteral("82"));

    if (data.isEmpty()) {
        // Alternativa UDS 22 1E 1C
        resp = m_interface->sendRawCommand(QStringLiteral("22 1E 1C"));
        data = PidDecoder::extractDataBytes(resp, QStringLiteral("62"), QStringLiteral("1E 1C"));
    }

    restoreFunctionalAddressing();

    if (!data.isEmpty()) {
        return PidDecoder::decodeToyotaTransTemp(static_cast<uint8_t>(data.at(0)));
    }
    return 0.0;
}

double UdsProtocol::readAbsWheelSpeed(TargetModule module)
{
    if (!m_interface) return 0.0;

    selectModule(module);
    double speed = 0.0;

    if (module == TargetModule::Brakes_ABS_Toyota) {
        QString resp = m_interface->sendRawCommand(QStringLiteral("21 01"));
        QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("61"), QStringLiteral("01"));
        if (!data.isEmpty()) {
            speed = static_cast<double>(static_cast<uint8_t>(data.at(0)));
        }
    } else {
        QString resp = m_interface->sendRawCommand(QStringLiteral("22 00 0D"));
        QByteArray data = PidDecoder::extractDataBytes(resp, QStringLiteral("62"), QStringLiteral("00 0D"));
        if (!data.isEmpty()) {
            speed = static_cast<double>(static_cast<uint8_t>(data.at(0)));
        }
    }

    restoreFunctionalAddressing();
    return speed;
}

