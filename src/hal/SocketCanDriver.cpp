#include "SocketCanDriver.h"
#include "../core/SafetyGuard.h"
#include <QElapsedTimer>
#include <QDebug>

SocketCanDriver::SocketCanDriver(QObject* parent)
    : IObdInterface(parent)
{
}

SocketCanDriver::~SocketCanDriver()
{
    closeConnection();
}

bool SocketCanDriver::openConnection(const QString& portName, int baudRate)
{
    Q_UNUSED(baudRate);

    if (m_canDevice) {
        closeConnection();
    }

    // Verificar si el plugin socketcan está disponible en el sistema (Linux/Embedded)
    if (!QCanBus::instance()->plugins().contains(QStringLiteral("socketcan"))) {
        emit errorOccurred(QStringLiteral("Plugin 'socketcan' no disponible en este sistema operativo"));
        return false;
    }

    QString errorString;
    m_canDevice.reset(QCanBus::instance()->createDevice(QStringLiteral("socketcan"), portName, &errorString));
    if (!m_canDevice) {
        emit errorOccurred(QStringLiteral("Error al crear dispositivo SocketCAN (%1): %2").arg(portName, errorString));
        return false;
    }

    connect(m_canDevice.get(), &QCanBusDevice::framesReceived, this, &SocketCanDriver::onFramesReceived);
    connect(m_canDevice.get(), &QCanBusDevice::errorOccurred, this, &SocketCanDriver::onErrorOccurred);

    if (!m_canDevice->connectDevice()) {
        emit errorOccurred(QStringLiteral("Error al conectar interfaz SocketCAN (%1): %2")
                               .arg(portName, m_canDevice->errorString()));
        m_canDevice.reset();
        return false;
    }

    m_connected = true;
    emit connectionStateChanged(true);
    return true;
}

void SocketCanDriver::closeConnection()
{
    if (m_canDevice && m_canDevice->state() == QCanBusDevice::ConnectedState) {
        m_canDevice->disconnectDevice();
    }
    m_canDevice.reset();

    if (m_connected) {
        m_connected = false;
        emit connectionStateChanged(false);
    }
}

bool SocketCanDriver::isConnected() const
{
    return m_connected && m_canDevice && (m_canDevice->state() == QCanBusDevice::ConnectedState);
}

void SocketCanDriver::setTransmitId(quint32 id)
{
    m_transmitId = id;
}

void SocketCanDriver::setReceiveFilter(quint32 id, quint32 mask)
{
    m_filterId = id;
    m_filterMask = mask;
}

void SocketCanDriver::onFramesReceived()
{
    if (!m_canDevice) return;
    while (m_canDevice->framesAvailable()) {
        m_receivedFrames.append(m_canDevice->readFrame());
    }
}

void SocketCanDriver::onErrorOccurred(QCanBusDevice::CanBusError error)
{
    Q_UNUSED(error);
    if (m_canDevice) {
        emit errorOccurred(QStringLiteral("Error en SocketCAN: %1").arg(m_canDevice->errorString()));
    }
}

QString SocketCanDriver::sendRawCommand(const QString& cmd, int timeoutMs)
{
    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("Interfaz SocketCAN no conectada"));
        return QString();
    }

    QString blockReason;
    if (!SafetyGuard::isCommandSafe(cmd, false, &blockReason)) {
        emit errorOccurred(QStringLiteral("Comando bloqueado por SafetyGuard: %1 (%2)").arg(cmd, blockReason));
        return QString();
    }

    QString trimmedCmd = cmd.trimmed().toUpper();

    // Emulación de configuración de headers AT para SocketCAN
    if (trimmedCmd.startsWith(QStringLiteral("AT SH"))) {
        QString idStr = trimmedCmd.mid(5).trimmed();
        bool ok = false;
        quint32 id = idStr.toUInt(&ok, 16);
        if (ok) {
            setTransmitId(id);
            return QStringLiteral("OK\r\r>");
        }
        return QStringLiteral("?\r\r>");
    }

    if (trimmedCmd.startsWith(QStringLiteral("AT CRA"))) {
        QString idStr = trimmedCmd.mid(6).trimmed();
        if (idStr.isEmpty()) {
            setReceiveFilter(0x7E8, 0x7F8);
        } else {
            bool ok = false;
            quint32 id = idStr.toUInt(&ok, 16);
            if (ok) {
                setReceiveFilter(id, 0x7FF);
            }
        }
        return QStringLiteral("OK\r\r>");
    }

    if (trimmedCmd.startsWith(QStringLiteral("AT"))) {
        return QStringLiteral("OK\r\r>");
    }

    // Convertir comando OBD/UDS hexadecimal a trama CAN (ISO 15765-2 Single Frame)
    QStringList tokens = trimmedCmd.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QByteArray payload;
    for (const QString& token : tokens) {
        bool ok = false;
        uchar byteVal = static_cast<uchar>(token.toUShort(&ok, 16));
        if (ok) {
            payload.append(static_cast<char>(byteVal));
        }
    }

    if (payload.isEmpty()) {
        return QStringLiteral("NO DATA\r\r>");
    }

    // Construir trama ISO-TP Single Frame (PCI = tamaño del payload)
    QByteArray canData;
    canData.append(static_cast<char>(payload.size()));
    canData.append(payload);
    while (canData.size() < 8) {
        canData.append(static_cast<char>(0xAA)); // Relleno estándar
    }

    QCanBusFrame txFrame(m_transmitId, canData);
    m_receivedFrames.clear();

    if (!m_canDevice->writeFrame(txFrame)) {
        emit errorOccurred(QStringLiteral("Error al escribir trama CAN: %1").arg(m_canDevice->errorString()));
        return QString();
    }

    // Esperar respuesta(s)
    QElapsedTimer timer;
    timer.start();
    QString formattedResponse;

    while (timer.elapsed() < timeoutMs) {
        int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) break;

        if (m_canDevice->waitForFramesReceived(remaining)) {
            while (m_canDevice->framesAvailable()) {
                m_receivedFrames.append(m_canDevice->readFrame());
            }

            for (const QCanBusFrame& rxFrame : m_receivedFrames) {
                quint32 rxId = rxFrame.frameId();
                if ((rxId & m_filterMask) == (m_filterId & m_filterMask) || rxId == m_filterId) {
                    QByteArray data = rxFrame.payload();
                    QString frameStr = QStringLiteral("%1 ").arg(rxId, 3, 16, QChar('0')).toUpper();
                    for (int i = 0; i < data.size(); ++i) {
                        frameStr.append(QStringLiteral("%1 ").arg(static_cast<uchar>(data.at(i)), 2, 16, QChar('0')).toUpper());
                    }
                    frameStr.append(QStringLiteral("\r"));
                    formattedResponse.append(frameStr);
                }
            }

            if (!formattedResponse.isEmpty()) {
                break;
            }
        }
    }

    if (formattedResponse.isEmpty()) {
        return QStringLiteral("NO DATA\r\r>");
    }

    formattedResponse.append(QStringLiteral("\r>"));
    emit rawDataReceived(formattedResponse);
    return formattedResponse;
}

