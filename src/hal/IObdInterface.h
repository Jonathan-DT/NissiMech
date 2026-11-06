#pragma once

#include <QObject>
#include <QString>

/**
 * @brief Interfaz pura de abstracción de hardware (HAL) para comunicación OBD2 / CAN.
 * 
 * Define el contrato estándar para drivers serie (ELM327, STN1110, STN2120),
 * adaptadores nativos SocketCAN y simuladores en memoria.
 */
class IObdInterface : public QObject
{
    Q_OBJECT

public:
    explicit IObdInterface(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IObdInterface() override = default;

    /**
     * @brief Abre la conexión con el adaptador o dispositivo de diagnóstico.
     * @param portName Nombre del puerto serie (ej. "/dev/ttyUSB0", "COM3") o interfaz CAN ("can0").
     * @param baudRate Velocidad en baudios (ej. 38400, 115200, 2000000).
     * @return true si la conexión física o lógica se estableció exitosamente.
     */
    virtual bool openConnection(const QString& portName, int baudRate = 115200) = 0;

    /**
     * @brief Cierra la conexión activa y libera los recursos de hardware.
     */
    virtual void closeConnection() = 0;

    /**
     * @brief Indica si el adaptador se encuentra actualmente conectado.
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Envía un comando en bruto al adaptador y espera sincrónicamente por la respuesta delimitada.
     * @param cmd Comando ASCII o hexadecimal a transmitir (sin necesidad de terminador explícito).
     * @param timeoutMs Tiempo máximo de espera en milisegundos.
     * @return Cadena con la respuesta recibida del adaptador.
     */
    virtual QString sendRawCommand(const QString& cmd, int timeoutMs = 2000) = 0;

signals:
    /**
     * @brief Emitida cuando se reciben datos en bruto desde el bus o adaptador.
     */
    void rawDataReceived(const QString& data);

    /**
     * @brief Emitida cuando ocurre un error de hardware, bus o sintaxis.
     */
    void errorOccurred(const QString& errorMsg);

    /**
     * @brief Emitida cuando cambia el estado de conexión del adaptador.
     */
    void connectionStateChanged(bool connected);
};

