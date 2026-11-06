#pragma once

#include "IObdInterface.h"
#include <QCanBus>
#include <QCanBusDevice>
#include <QCanBusFrame>
#include <memory>

/**
 * @brief Driver nativo para bus CAN en Linux y Linux Embebido (ARM64)
 * utilizando el subsistema SocketCAN a través de QtSerialBus.
 */
class SocketCanDriver : public IObdInterface
{
    Q_OBJECT

public:
    explicit SocketCanDriver(QObject* parent = nullptr);
    ~SocketCanDriver() override;

    bool openConnection(const QString& portName, int baudRate = 500000) override;
    void closeConnection() override;
    bool isConnected() const override;
    QString sendRawCommand(const QString& cmd, int timeoutMs = 2000) override;

    /**
     * @brief Configura el ID CAN de transmisión (por defecto 0x7DF para OBD2 funcional).
     */
    void setTransmitId(quint32 id);

    /**
     * @brief Configura el filtro CAN para recepción (por defecto 0x7E8..0x7EF).
     */
    void setReceiveFilter(quint32 id, quint32 mask = 0x7F8);

private slots:
    void onFramesReceived();
    void onErrorOccurred(QCanBusDevice::CanBusError error);

private:
    std::unique_ptr<QCanBusDevice> m_canDevice;
    bool m_connected{false};
    quint32 m_transmitId{0x7DF};
    quint32 m_filterId{0x7E8};
    quint32 m_filterMask{0x7F8};
    QList<QCanBusFrame> m_receivedFrames;
};

