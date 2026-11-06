#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <optional>
#include "../hal/IObdInterface.h"
#include "DtcManager.h"
#include "ObdbJsonLoader.h"
#include "DbcParser.h"

/**
 * @brief Controlador desacoplado del protocolo OBD2 (SAE J1979 / ISO 15765-4).
 * Alimentado dinámicamente por archivos OBDb y Vector DBC sin fórmulas hardcodeadas.
 */
class ObdProtocol : public QObject
{
    Q_OBJECT

public:
    explicit ObdProtocol(IObdInterface* interface,
                        DtcManager* dtcManager,
                        ObdbJsonLoader* obdbLoader = nullptr,
                        DbcParser* dbcParser = nullptr,
                        QObject* parent = nullptr);
    ~ObdProtocol() override = default;

    void setObdbLoader(ObdbJsonLoader* loader) { m_obdbLoader = loader; }
    void setDbcParser(DbcParser* parser) { m_dbcParser = parser; }

    /**
     * @brief Detecta de forma segura el protocolo físico activo en el vehículo.
     * Envía 'AT SP 0', '01 00', 'AT DP' y 'AT DPN'.
     */
    QString detectActiveProtocol();

    /**
     * @brief Consulta el valor físico de una señal registrada en OBDb o DBC.
     * @param signalId Identificador de señal (ej: "SAE_ENGINE_RPM", "CAMRY_ENGINE_SPEED_V2", "SPARK_VSS").
     * @return Valor numérico en unidades de ingeniería o std::nullopt si no existe o falla.
     */
    std::optional<double> querySignal(const QString& signalId);

    /**
     * @brief Lee la tensión de la batería reportada por el adaptador ELM327 (AT RV).
     */
    double readBatteryVoltage();

    // Gestión de fallas DTC (SAE J1979)
    QList<DtcInfo> readStoredDtcs();
    QList<DtcInfo> readPendingDtcs();
    bool clearDtcs(bool userConfirmed);

    // Identificación de vehículo
    QString readVin();

    /**
     * @brief Configura el direccionamiento funcional estándar (7DF).
     */
    bool setFunctionalAddressing();

    /**
     * @brief Configura el direccionamiento hacia un encabezado y filtro específico.
     */
    bool setAddressing(const QString& header, const QString& cra = QString());

    /**
     * @brief Reensambla tramas multiframe ISO-TP (First Frame 10 xx, Consecutive 21, 22...)
     */
    static QByteArray reassembleIsoTpPayload(const QString& rawResponse);

private:
    IObdInterface* m_interface{nullptr};
    DtcManager* m_dtcManager{nullptr};
    ObdbJsonLoader* m_obdbLoader{nullptr};
    DbcParser* m_dbcParser{nullptr};
    QString m_activeProtocolDescription;
};
