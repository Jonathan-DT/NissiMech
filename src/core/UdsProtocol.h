#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <optional>
#include <cstdint>
#include "../hal/IObdInterface.h"
#include "DtcManager.h"
#include "ObdbJsonLoader.h"
#include "DbcParser.h"

/**
 * @brief Controlador para servicios extendidos de diagnóstico vehicular UDS (ISO 14229 / ISO 15765).
 * 
 * Gestiona el enrutamiento dinámico a ECUs individuales (ECM, TCM, ABS, etc.) basado
 * estrictamente en las cabeceras y señales de los archivos cargados (sin suposiciones hardcodeadas).
 */
class UdsProtocol : public QObject
{
    Q_OBJECT

public:
    explicit UdsProtocol(IObdInterface* interface,
                         DtcManager* dtcManager,
                         ObdbJsonLoader* obdbLoader = nullptr,
                         DbcParser* dbcParser = nullptr,
                         QObject* parent = nullptr);
    ~UdsProtocol() override = default;

    void setObdbLoader(ObdbJsonLoader* loader) { m_obdbLoader = loader; }
    void setDbcParser(DbcParser* parser) { m_dbcParser = parser; }

    /**
     * @brief Configura el direccionamiento físico hacia una ECU por sus identificadores CAN.
     */
    bool selectPhysicalModule(const QString& txHeader, const QString& rxCra = QString());

    /**
     * @brief Restaura el direccionamiento funcional estándar (Broadcast 7DF).
     */
    bool restoreFunctionalAddressing();

    /**
     * @brief Lee los códigos DTC de un módulo específico mediante UDS Servicio 0x19 0x02.
     * @param txHeader Cabecera de transmisión (ej. "7E0", "7E1", "7E2", etc.).
     * @param rxCra Filtro de recepción esperado (ej. "7E8", "7E9", "7EA", etc.).
     */
    QList<DtcInfo> readModuleDtcs(const QString& txHeader, const QString& rxCra = QString());

    /**
     * @brief Lee un parámetro mediante UDS ReadDataByIdentifier (Servicio 0x22).
     */
    QByteArray readDataByIdentifier(const QString& txHeader, uint16_t did, const QString& rxCra = QString());

    /**
     * @brief Consulta el valor físico de una señal específica registrada en el archivo OBDb del vehículo.
     */
    std::optional<double> querySignal(const QString& signalId);

private:
    IObdInterface* m_interface{nullptr};
    DtcManager* m_dtcManager{nullptr};
    ObdbJsonLoader* m_obdbLoader{nullptr};
    DbcParser* m_dbcParser{nullptr};
};
