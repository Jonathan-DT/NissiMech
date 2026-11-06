#pragma once

#include "IObdInterface.h"
#include <QSerialPort>
#include <QElapsedTimer>
#include <memory>

/**
 * @brief Driver de comunicación serie para interfaces ELM327, STN1110, STN2120
 * y cables profesionales como Vgate vLinker FS USB y OBDLink EX USB.
 */
class SerialElmDriver : public IObdInterface
{
    Q_OBJECT

public:
    explicit SerialElmDriver(QObject* parent = nullptr);
    ~SerialElmDriver() override;

    bool openConnection(const QString& portName, int baudRate = 115200) override;
    void closeConnection() override;
    bool isConnected() const override;
    QString sendRawCommand(const QString& cmd, int timeoutMs = 2000) override;

    /**
     * @brief Ejecuta la secuencia obligatoria de inicialización segura del chip OBD.
     * @return true si el chip respondió correctamente a los comandos de inicialización.
     */
    bool initializeAdapter();

    /**
     * @brief Obtiene la versión / firma detectada del chip adaptador.
     */
    QString adapterSignature() const;

private:
    /**
     * @brief Verifica si la respuesta contiene errores conocidos de ELM327 / STN.
     */
    bool parseErrors(const QString& response);

    QSerialPort m_serialPort;
    bool m_connected{false};
    QString m_adapterSignature;
};

