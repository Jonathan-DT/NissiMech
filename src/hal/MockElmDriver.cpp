#include "MockElmDriver.h"
#include "../core/SafetyGuard.h"
#include <QDebug>

MockElmDriver::MockElmDriver(QObject* parent)
    : IObdInterface(parent)
{
}

bool MockElmDriver::openConnection(const QString& portName, int baudRate)
{
    Q_UNUSED(portName);
    Q_UNUSED(baudRate);
    m_connected = true;
    emit connectionStateChanged(true);
    return true;
}

void MockElmDriver::closeConnection()
{
    if (m_connected) {
        m_connected = false;
        emit connectionStateChanged(false);
    }
}

bool MockElmDriver::isConnected() const
{
    return m_connected;
}

void MockElmDriver::setVehicleProfile(VehicleProfile profile)
{
    m_profile = profile;
}

MockElmDriver::VehicleProfile MockElmDriver::vehicleProfile() const
{
    return m_profile;
}

void MockElmDriver::setCurrentHeader(const QString& header)
{
    m_currentHeader = header.trimmed().toUpper();
}

QString MockElmDriver::currentHeader() const
{
    return m_currentHeader;
}

QString MockElmDriver::sendRawCommand(const QString& cmd, int timeoutMs)
{
    Q_UNUSED(timeoutMs);

    if (!m_connected) {
        emit errorOccurred(QStringLiteral("Error: El simulador OBD no está conectado."));
        return QString();
    }

    // Validación por el cortafuegos de seguridad de ECU
    QString blockReason;
    if (!SafetyGuard::isCommandSafe(cmd, SafetyGuard::isUserConfirmedClearDtc(), &blockReason)) {
        emit errorOccurred(QStringLiteral("Comando bloqueado por SafetyGuard: %1 (%2)").arg(cmd, blockReason));
        return QString();
    }

    QString normalized = cmd.trimmed().toUpper();

    // Actualización de cabeceras AT SH y filtros AT CRA
    if (normalized.startsWith(QStringLiteral("AT SH"))) {
        m_currentHeader = normalized.mid(5).trimmed();
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized.startsWith(QStringLiteral("AT CRA"))) {
        m_currentCra = normalized.mid(6).trimmed();
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT H1")) {
        m_headersEnabled = true;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT H0")) {
        m_headersEnabled = false;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT E0")) {
        m_echoEnabled = false;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT E1")) {
        m_echoEnabled = true;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }

    QString response;
    if (m_profile == VehicleProfile::ToyotaCamry2006) {
        response = processToyotaCommand(normalized);
    } else {
        response = processBeatCommand(normalized);
    }

    emit rawDataReceived(response);
    return response;
}

QString MockElmDriver::processToyotaCommand(const QString& cmd)
{
    // Firma e identificación del chip
    if (cmd == QStringLiteral("AT Z") || cmd == QStringLiteral("ATZ")) {
        return QStringLiteral("ELM327 v1.4b (Mock Vgate vLinker FS)\r\r>");
    }

    // Comandos de configuración
    if (cmd == QStringLiteral("AT L0") || cmd == QStringLiteral("AT S1") ||
        cmd == QStringLiteral("AT CAF1") || cmd == QStringLiteral("AT CFC1") ||
        cmd == QStringLiteral("AT CSM1") || cmd == QStringLiteral("AT AT1") ||
        cmd == QStringLiteral("AT SP 0") || cmd == QStringLiteral("AT SP0")) {
        return QStringLiteral("OK\r\r>");
    }

    if (cmd == QStringLiteral("AT DP")) {
        return QStringLiteral("AUTO, ISO 15765-4 (CAN 11/500)\r\r>");
    }
    if (cmd == QStringLiteral("AT DPN")) {
        return QStringLiteral("6\r\r>");
    }
    if (cmd == QStringLiteral("AT RV")) {
        return QStringLiteral("12.6V\r\r>");
    }

    // Dirección al módulo ABS Toyota (7B0)
    if (m_currentHeader == QStringLiteral("7B0")) {
        if (cmd == QStringLiteral("19 02 FF") || cmd == QStringLiteral("1902FF")) {
            return QStringLiteral("7B8 06 59 02 FF C0 20 00 2F \r\r>");
        }
        if (cmd == QStringLiteral("21 01") || cmd == QStringLiteral("2101")) {
            return QStringLiteral("7B8 06 61 01 00 00 00 00 \r\r>");
        }
        return QStringLiteral("7B8 03 7F 22 11 \r\r>");
    }

    // Dirección al módulo de Transmisión Toyota TCM (7E1)
    if (m_currentHeader == QStringLiteral("7E1")) {
        if (cmd == QStringLiteral("21 82") || cmd == QStringLiteral("2182") ||
            cmd == QStringLiteral("22 1E 1C") || cmd == QStringLiteral("221E1C")) {
            return QStringLiteral("7E9 04 61 82 6E \r\r>");
        }
        if (cmd == QStringLiteral("01 0C") || cmd == QStringLiteral("010C")) {
            return QStringLiteral("7E9 04 41 0C 0B B8 \r\r>");
        }
        if (cmd == QStringLiteral("03")) {
            return QStringLiteral("7E9 04 43 01 07 40 \r\r>");
        }
    }

    // Funcional broadcast (7DF) o Físico Motor (7E0)
    if (cmd == QStringLiteral("01 00") || cmd == QStringLiteral("0100")) {
        return QStringLiteral("7E8 06 41 00 BE 3F B8 13 \r7E9 06 41 00 80 10 80 00 \r\r>");
    }
    if (cmd == QStringLiteral("01 0C") || cmd == QStringLiteral("010C")) {
        return QStringLiteral("7E8 04 41 0C 0B B8 \r\r>");
    }
    if (cmd == QStringLiteral("01 05") || cmd == QStringLiteral("0105")) {
        return QStringLiteral("7E8 03 41 05 82 \r\r>");
    }
    if (cmd == QStringLiteral("01 0D") || cmd == QStringLiteral("010D")) {
        return QStringLiteral("7E8 03 41 0D 00 \r\r>");
    }
    if (cmd == QStringLiteral("01 0F") || cmd == QStringLiteral("010F")) {
        return QStringLiteral("7E8 03 41 0F 46 \r\r>"); // 30 °C
    }
    if (cmd == QStringLiteral("01 10") || cmd == QStringLiteral("0110")) {
        return QStringLiteral("7E8 04 41 10 01 54 \r\r>"); // 3.4 g/s
    }
    if (cmd == QStringLiteral("01 11") || cmd == QStringLiteral("0111")) {
        return QStringLiteral("7E8 03 41 11 26 \r\r>"); // 14.9 %
    }
    if (cmd == QStringLiteral("01 42") || cmd == QStringLiteral("0142")) {
        return QStringLiteral("7E8 04 41 42 34 56 \r\r>"); // 13.4 V
    }
    if (cmd == QStringLiteral("03")) {
        return QStringLiteral("7E8 06 43 02 01 71 07 40 \r\r>");
    }
    if (cmd == QStringLiteral("07")) {
        return QStringLiteral("7E8 04 47 01 03 00 \r\r>");
    }
    if (cmd == QStringLiteral("04")) {
        return QStringLiteral("7E8 01 44 \r\r>");
    }
    if (cmd == QStringLiteral("09 02") || cmd == QStringLiteral("0902")) {
        // VIN Toyota Camry 2006 (4T1BE32K06U123456)
        return QStringLiteral("7E8 10 14 49 02 01 34 54 31 \r7E8 21 42 45 33 32 4B 30 36 \r7E8 22 55 31 32 33 34 35 36 \r\r>");
    }

    return QStringLiteral("NO DATA\r\r>");
}

QString MockElmDriver::processBeatCommand(const QString& cmd)
{
    // Firma e identificación del chip
    if (cmd == QStringLiteral("AT Z") || cmd == QStringLiteral("ATZ")) {
        return QStringLiteral("ELM327 v2.2 (Mock STN2120 / vLinker)\r\r>");
    }

    // Comandos de configuración
    if (cmd == QStringLiteral("AT L0") || cmd == QStringLiteral("AT S1") ||
        cmd == QStringLiteral("AT CAF1") || cmd == QStringLiteral("AT CFC1") ||
        cmd == QStringLiteral("AT CSM1") || cmd == QStringLiteral("AT AT1") ||
        cmd == QStringLiteral("AT SP 0") || cmd == QStringLiteral("AT SP0")) {
        return QStringLiteral("OK\r\r>");
    }

    if (cmd == QStringLiteral("AT DP")) {
        return QStringLiteral("AUTO, ISO 15765-4 (CAN 11/500)\r\r>");
    }
    if (cmd == QStringLiteral("AT DPN")) {
        return QStringLiteral("6\r\r>");
    }
    if (cmd == QStringLiteral("AT RV")) {
        return QStringLiteral("14.1V\r\r>");
    }

    // Módulo ABS Chevrolet Beat GM EBCM (7E2)
    if (m_currentHeader == QStringLiteral("7E2")) {
        if (cmd == QStringLiteral("19 02 FF") || cmd == QStringLiteral("1902FF")) {
            return QStringLiteral("7EA 06 59 02 FF C0 03 50 2F \r\r>");
        }
        if (cmd == QStringLiteral("22 00 0D") || cmd == QStringLiteral("22000D")) {
            return QStringLiteral("7EA 04 62 00 0D 00 \r\r>");
        }
        return QStringLiteral("7EA 03 7F 22 11 \r\r>");
    }

    // Comandos estándar de motor (ECM)
    if (cmd == QStringLiteral("01 00") || cmd == QStringLiteral("0100")) {
        return QStringLiteral("7E8 06 41 00 BE 3E B8 11 \r\r>");
    }
    if (cmd == QStringLiteral("01 0C") || cmd == QStringLiteral("010C")) {
        return QStringLiteral("7E8 04 41 0C 0C 80 \r\r>"); // 800 RPM (0x0C80 = 3200 -> 3200/4 = 800)
    }
    if (cmd == QStringLiteral("01 05") || cmd == QStringLiteral("0105")) {
        return QStringLiteral("7E8 03 41 05 87 \r\r>"); // 95 °C (0x87 = 135 -> 135 - 40 = 95)
    }
    if (cmd == QStringLiteral("01 0D") || cmd == QStringLiteral("010D")) {
        return QStringLiteral("7E8 03 41 0D 00 \r\r>");
    }
    if (cmd == QStringLiteral("01 0F") || cmd == QStringLiteral("010F")) {
        return QStringLiteral("7E8 03 41 0F 41 \r\r>"); // 25 °C
    }
    if (cmd == QStringLiteral("01 10") || cmd == QStringLiteral("0110")) {
        return QStringLiteral("7E8 04 41 10 00 F0 \r\r>"); // 2.4 g/s
    }
    if (cmd == QStringLiteral("01 11") || cmd == QStringLiteral("0111")) {
        return QStringLiteral("7E8 03 41 11 20 \r\r>"); // 12.5 %
    }
    if (cmd == QStringLiteral("01 42") || cmd == QStringLiteral("0142")) {
        return QStringLiteral("7E8 04 41 42 36 B0 \r\r>"); // 14.0 V
    }
    if (cmd == QStringLiteral("03")) {
        return QStringLiteral("7E8 04 43 01 01 71 \r\r>"); // P0171
    }
    if (cmd == QStringLiteral("07")) {
        return QStringLiteral("7E8 02 47 00 \r\r>");
    }
    if (cmd == QStringLiteral("04")) {
        return QStringLiteral("7E8 01 44 \r\r>");
    }
    if (cmd == QStringLiteral("09 02") || cmd == QStringLiteral("0902")) {
        // VIN Chevrolet Beat 2018 Multiframe: MA6CB6CD9JT000001
        return QStringLiteral("7E8 10 14 49 02 01 4D 41 36 \r"
                              "7E8 21 43 42 36 43 44 39 4A \r"
                              "7E8 22 54 30 30 30 30 30 31 \r\r>");
    }

    return QStringLiteral("NO DATA\r\r>");
}

