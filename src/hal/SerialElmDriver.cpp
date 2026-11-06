#include "SerialElmDriver.h"
#include "../core/SafetyGuard.h"
#include <QThread>
#include <QDebug>

SerialElmDriver::SerialElmDriver(QObject* parent)
    : IObdInterface(parent)
{
}

SerialElmDriver::~SerialElmDriver()
{
    closeConnection();
}

bool SerialElmDriver::openConnection(const QString& portName, int baudRate)
{
    if (m_serialPort.isOpen()) {
        closeConnection();
    }

    m_serialPort.setPortName(portName);
    m_serialPort.setBaudRate(baudRate);
    m_serialPort.setDataBits(QSerialPort::Data8);
    m_serialPort.setParity(QSerialPort::NoParity);
    m_serialPort.setStopBits(QSerialPort::OneStop);
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        emit errorOccurred(QStringLiteral("No se pudo abrir el puerto serie %1: %2")
                               .arg(portName, m_serialPort.errorString()));
        return false;
    }

    // Limpiar buffers residuales
    m_serialPort.clear();

    // Ejecutar inicialización del chip ELM327 / STN
    if (!initializeAdapter()) {
        emit errorOccurred(QStringLiteral("Fallo en la secuencia de inicialización del adaptador en %1").arg(portName));
        m_serialPort.close();
        return false;
    }

    m_connected = true;
    emit connectionStateChanged(true);
    return true;
}

void SerialElmDriver::closeConnection()
{
    if (m_serialPort.isOpen()) {
        m_serialPort.close();
    }
    if (m_connected) {
        m_connected = false;
        emit connectionStateChanged(false);
    }
}

bool SerialElmDriver::isConnected() const
{
    return m_connected && m_serialPort.isOpen();
}

QString SerialElmDriver::sendRawCommand(const QString& cmd, int timeoutMs)
{
    if (!m_serialPort.isOpen()) {
        emit errorOccurred(QStringLiteral("Error: El puerto serie no está abierto."));
        return QString();
    }

    // Regla de seguridad de ECU obligatoria
    QString blockReason;
    if (!SafetyGuard::isCommandSafe(cmd, SafetyGuard::isUserConfirmedClearDtc(), &blockReason)) {
        emit errorOccurred(QStringLiteral("Comando bloqueado por SafetyGuard: %1 (%2)").arg(cmd, blockReason));
        return QString();
    }

    QString formattedCmd = cmd.trimmed() + QLatin1Char('\r');
    m_serialPort.clear(); // Limpiar datos pendientes antes de transmitir

    qint64 bytesWritten = m_serialPort.write(formattedCmd.toLatin1());
    if (bytesWritten == -1) {
        emit errorOccurred(QStringLiteral("Error al escribir en el puerto serie: %1").arg(m_serialPort.errorString()));
        return QString();
    }
    m_serialPort.flush();

    QString responseBuffer;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) break;

        if (m_serialPort.waitForReadyRead(remaining)) {
            QByteArray chunk = m_serialPort.readAll();
            // Filtrar nulos ocasionales (0x00) insertados por UART/ELM327 según hoja de datos
            chunk.replace('\0', QByteArray());
            responseBuffer.append(QString::fromLatin1(chunk));

            // El fin de mensaje en ELM327 está delimitado por el carácter prompt '>'
            if (responseBuffer.contains(QLatin1Char('>'))) {
                break;
            }
        }
    }

    if (!responseBuffer.contains(QLatin1Char('>'))) {
        emit errorOccurred(QStringLiteral("Tiempo de espera agotado (timeout) esperando prompt '>' para comando: %1").arg(cmd));
        return QString();
    }

    // Limpiar caracteres del prompt final y retorno
    int promptIdx = responseBuffer.lastIndexOf(QLatin1Char('>'));
    if (promptIdx >= 0) {
        responseBuffer = responseBuffer.left(promptIdx);
    }

    emit rawDataReceived(responseBuffer);
    parseErrors(responseBuffer);

    return responseBuffer;
}

bool SerialElmDriver::initializeAdapter()
{
    // Secuencia de inicialización obligatoria y segura
    // 1. AT Z: Reset completo, esperar hasta 1500 ms por la firma
    QString zResp = sendRawCommand(QStringLiteral("AT Z"), 1500);
    if (zResp.isEmpty()) {
        return false;
    }
    m_adapterSignature = zResp.trimmed();

    // Comandos de configuración estándar
    const QStringList initSequence = {
        QStringLiteral("AT E0"),   // 2. Desactivar eco
        QStringLiteral("AT L0"),   // 3. Desactivar saltos de línea adicionales
        QStringLiteral("AT S1"),   // 4. Mantener espacios entre bytes
        QStringLiteral("AT H1"),   // 5. Activar encabezados (Headers 7E8, 7E9, etc.)
        QStringLiteral("AT CAF1"), // 6. Activar formato automático CAN ISO 15765-2
        QStringLiteral("AT CFC1"), // 7. Activar control de flujo automático CAN
        QStringLiteral("AT CSM1"), // 8. CAN Silent Monitoring activo por seguridad
        QStringLiteral("AT AT1"),  // 9. Adaptive Timing auto1
        QStringLiteral("AT SP 0")  // 10. Detección automática de protocolo
    };

    for (const QString& initCmd : initSequence) {
        QString resp = sendRawCommand(initCmd, 1000);
        if (resp.isEmpty() || resp.contains(QLatin1Char('?')) || resp.contains(QStringLiteral("ERROR"))) {
            qWarning() << "Advertencia en inicialización con comando" << initCmd << ":" << resp;
        }
    }

    return true;
}

QString SerialElmDriver::adapterSignature() const
{
    return m_adapterSignature;
}

bool SerialElmDriver::parseErrors(const QString& response)
{
    if (response.contains(QLatin1Char('?'))) {
        emit errorOccurred(QStringLiteral("Error de sintaxis reportado por el adaptador (?)"));
        return true;
    }
    if (response.contains(QStringLiteral("NO DATA"))) {
        emit errorOccurred(QStringLiteral("La ECU no respondió (NO DATA)"));
        return true;
    }
    if (response.contains(QStringLiteral("UNABLE TO CONNECT"))) {
        emit errorOccurred(QStringLiteral("No fue posible conectar con el vehículo (UNABLE TO CONNECT)"));
        return true;
    }
    if (response.contains(QStringLiteral("BUS INIT: ... ERROR")) || response.contains(QStringLiteral("BUS INIT: ERROR"))) {
        emit errorOccurred(QStringLiteral("Error en inicialización del bus OBD"));
        return true;
    }
    if (response.contains(QStringLiteral("BUS ERROR"))) {
        emit errorOccurred(QStringLiteral("Error eléctrico o de trama en el bus (BUS ERROR)"));
        return true;
    }
    if (response.contains(QStringLiteral("FB ERROR"))) {
        emit errorOccurred(QStringLiteral("Error de feedback CAN (FB ERROR)"));
        return true;
    }
    if (response.contains(QStringLiteral("CAN ERROR"))) {
        emit errorOccurred(QStringLiteral("Error en transceptor CAN (CAN ERROR)"));
        return true;
    }
    if (response.contains(QStringLiteral("<RX ERROR"))) {
        emit errorOccurred(QStringLiteral("Error de recepción CAN (<RX ERROR)"));
        return true;
    }
    if (response.contains(QStringLiteral("ERR94"))) {
        emit errorOccurred(QStringLiteral("Falla fatal CAN (ERR94). Requiere reinicio AT Z"));
        return true;
    }

    return false;
}

