#pragma once

#include "IObdInterface.h"

/**
 * @brief Simulador en memoria de adaptador ELM327 / STN y ECU vehicular.
 * Permite ejecutar pruebas completas y desarrollo de UI sin requerir hardware físico.
 */
class MockElmDriver : public IObdInterface
{
    Q_OBJECT

public:
    enum class VehicleProfile {
        ToyotaCamry2006,
        ChevroletBeat2018
    };
    Q_ENUM(VehicleProfile)

    explicit MockElmDriver(QObject* parent = nullptr);
    ~MockElmDriver() override = default;

    bool openConnection(const QString& portName = QStringLiteral("MOCK_OBD"), int baudRate = 115200) override;
    void closeConnection() override;
    bool isConnected() const override;
    QString sendRawCommand(const QString& cmd, int timeoutMs = 2000) override;

    void setVehicleProfile(VehicleProfile profile);
    VehicleProfile vehicleProfile() const;

    void setCurrentHeader(const QString& header);
    QString currentHeader() const;

private:
    QString processToyotaCommand(const QString& cmd);
    QString processBeatCommand(const QString& cmd);

    bool m_connected{false};
    VehicleProfile m_profile{VehicleProfile::ToyotaCamry2006};
    QString m_currentHeader{QStringLiteral("7DF")};
    QString m_currentCra;
    bool m_headersEnabled{true};
    bool m_echoEnabled{false};
};

