#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <cstdint>
#include "../hal/IObdInterface.h"
#include "DtcManager.h"

/**
 * @brief Módulos electrónicos destino para diagnóstico avanzado UDS (ISO 14229)
 * y direccionamiento físico específico de fabricante.
 */
enum class TargetModule {
    Engine_ECM,
    Transmission_TCM,
    Brakes_ABS_ISO,        // Chevrolet Beat / Spark GM EBCM (Tx: 7E2, Rx: 7EA)
    Brakes_ABS_Toyota,     // Toyota Camry / Corolla ABS (Tx: 7B0, Rx: 7B8)
    Brakes_ABS_Ford_MSCAN  // Ford MS-CAN ABS (Tx: 760, Rx: 768)
};

/**
 * @brief Controlador para servicios extendidos de diagnóstico vehicular UDS (ISO 14229 / ISO 15765).
 * 
 * Gestiona el enrutamiento físico a ECUs individuales (ECM, TCM, ABS), lectura de DIDs,
 * lectura de códigos de falla específicos con status mask (Servicio 0x19) y control de flujo.
 */
class UdsProtocol : public QObject
{
    Q_OBJECT

public:
    explicit UdsProtocol(IObdInterface* interface, DtcManager* dtcManager, QObject* parent = nullptr);
    ~UdsProtocol() override = default;

    /**
     * @brief Configura el direccionamiento físico del adaptador hacia una ECU específica.
     */
    bool selectModule(TargetModule module);

    /**
     * @brief Restaura el direccionamiento funcional estándar (Broadcast 7DF con filtro abierto).
     */
    bool restoreFunctionalAddressing();

    /**
     * @brief Lee los códigos DTC de un módulo específico mediante UDS Servicio 0x19 0x02.
     * @param module Módulo vehicular a consultar.
     * @return Lista de fallas detectadas y enriquecidas con la base de datos clínica.
     */
    QList<DtcInfo> readModuleDtcs(TargetModule module);

    /**
     * @brief Lee un parámetro mediante UDS ReadDataByIdentifier (Servicio 0x22) o 0x21 (Toyota).
     * @param module Módulo objetivo.
     * @param did Identificador de datos (Data Identifier DID).
     * @return Arreglo de bytes con los datos recibidos de la ECU.
     */
    QByteArray readDataByIdentifier(TargetModule module, uint16_t did);

    /**
     * @brief Lee la temperatura del fluido de la transmisión (TCM) en vehículos Toyota.
     */
    double readToyotaTransmissionFluidTemp();

    /**
     * @brief Lee la velocidad del sensor de rueda ABS.
     */
    double readAbsWheelSpeed(TargetModule module);

private:
    IObdInterface* m_interface{nullptr};
    DtcManager* m_dtcManager{nullptr};
};

