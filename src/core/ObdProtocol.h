#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <memory>
#include "../hal/IObdInterface.h"
#include "DtcManager.h"

/**
 * @brief Controlador del protocolo OBD2 estándar (SAE J1979 / ISO 15765-4).
 * 
 * Gestiona el direccionamiento funcional ($7DF) y físico ($7E0), decodificación
 * de flujos de datos en tiempo real, lectura/borrado seguro de fallas y reensamblado
 * de tramas multiframe ISO-TP (ej. lectura de VIN).
 */
class ObdProtocol : public QObject
{
    Q_OBJECT

public:
    explicit ObdProtocol(IObdInterface* interface, DtcManager* dtcManager, QObject* parent = nullptr);
    ~ObdProtocol() override = default;

    /**
     * @brief Configura el direccionamiento funcional estándar (Broadcast a todas las ECUs: 7DF).
     */
    bool setFunctionalAddressing();

    /**
     * @brief Configura el direccionamiento físico al módulo del motor (ECM: 7E0 con filtro 7E8).
     */
    bool setPhysicalEngineAddressing();

    // Métodos de consulta de datos en tiempo real (Modo 01)
    double readRpm();
    double readSpeed();
    double readCoolantTemp();
    double readIntakeAirTemp();
    double readMaf();
    double readThrottle();
    double readControlModuleVoltage();
    double readBatteryVoltage();

    // Métodos de gestión de fallas (Modos 03, 04, 07)
    QList<DtcInfo> readStoredDtcs();
    QList<DtcInfo> readPendingDtcs();
    bool clearDtcs(bool userConfirmed);

    // Identificación del vehículo (Modo 09)
    QString readVin();

    /**
     * @brief Reensambla tramas multiframe ISO-TP (First Frame 10 xx, Consecutive 21, 22...)
     * @param rawResponse Respuesta textual del bus CAN con múltiples tramas.
     * @return Arreglo de bytes del payload completo reensamblado.
     */
    static QByteArray reassembleIsoTpPayload(const QString& rawResponse);

private:
    IObdInterface* m_interface{nullptr};
    DtcManager* m_dtcManager{nullptr};
};

