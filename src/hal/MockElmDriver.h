#pragma once

#include "IObdInterface.h"
#include <QMap>
#include <QPair>

/**
 * @brief Simulador dinámico en memoria de adaptador ELM327 / STN y ECUs vehiculares.
 * Carga respuestas estrictamente desde archivos de escenarios JSON (database/scenarios/escenario.json),
 * devolviendo "NO DATA\r\r>" ante cualquier comando no documentado.
 */
class MockElmDriver : public IObdInterface
{
    Q_OBJECT

public:
    explicit MockElmDriver(QObject* parent = nullptr);
    ~MockElmDriver() override = default;

    bool openConnection(const QString& portName = QStringLiteral("MOCK_OBD"), int baudRate = 115200) override;
    void closeConnection() override;
    bool isConnected() const override;
    QString sendRawCommand(const QString& cmd, int timeoutMs = 2000) override;

    /**
     * @brief Carga un archivo de escenario JSON dinámico para la simulación.
     */
    bool loadScenario(const QString& scenarioPath);

    /**
     * @brief Establece o sobrescribe una respuesta simulada para pruebas dinámicas.
     */
    void setResponse(const QString& command, const QString& response, const QString& header = QStringLiteral("*"));

    QString scenarioName() const { return m_scenarioName; }
    QString vehicleFolder() const { return m_vehicleFolder; }

    void setCurrentHeader(const QString& header);
    QString currentHeader() const { return m_currentHeader; }

    void setCurrentCra(const QString& cra);
    QString currentCra() const { return m_currentCra; }

private:
    QString lookupResponse(const QString& cmd);

    bool m_connected{false};
    QString m_scenarioName;
    QString m_vehicleFolder;
    QString m_currentHeader{QStringLiteral("7DF")};
    QString m_currentCra;
    bool m_headersEnabled{true};
    bool m_echoEnabled{false};

    // Mapeo: Pair<Header, NormalizedCommand> -> ResponseString
    QMap<QPair<QString, QString>, QString> m_scenarioResponses;
    // Respuestas independientes de cabecera (header "*")
    QMap<QString, QString> m_wildcardResponses;
};
