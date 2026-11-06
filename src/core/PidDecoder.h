#pragma once

#include <QString>
#include <QByteArray>
#include <cstdint>

/**
 * @brief Decodificador de parámetros en tiempo real (PIDs) según SAE J1979 / ISO 15031-5.
 */
class PidDecoder
{
public:
    PidDecoder() = delete;

    /**
     * @brief PID 0C - Régimen del motor (RPM)
     * Fórmula: ((A * 256) + B) / 4.0
     */
    static double decodeRpm(uint8_t a, uint8_t b);

    /**
     * @brief PID 0D - Velocidad del vehículo (km/h)
     * Fórmula: A
     */
    static double decodeSpeed(uint8_t a);

    /**
     * @brief PID 05 - Temperatura del refrigerante del motor (°C)
     * Fórmula: A - 40.0
     */
    static double decodeCoolantTemp(uint8_t a);

    /**
     * @brief PID 0F - Temperatura del aire de admisión (°C)
     * Fórmula: A - 40.0
     */
    static double decodeIntakeAirTemp(uint8_t a);

    /**
     * @brief PID 10 - Caudal de masa de aire MAF (g/s)
     * Fórmula: ((A * 256) + B) / 100.0
     */
    static double decodeMaf(uint8_t a, uint8_t b);

    /**
     * @brief PID 11 - Posición absoluta del acelerador TPS (%)
     * Fórmula: (A * 100.0) / 255.0
     */
    static double decodeThrottle(uint8_t a);

    /**
     * @brief PID 42 - Tensión del módulo de control (V)
     * Fórmula: ((A * 256) + B) / 1000.0
     */
    static double decodeControlModuleVoltage(uint8_t a, uint8_t b);

    /**
     * @brief Transmisión Toyota - Comando 21 82 (°C)
     * Fórmula: A - 40.0
     */
    static double decodeToyotaTransTemp(uint8_t a);

    /**
     * @brief Extrae los bytes de datos de una respuesta bruta ELM327 filtrando headers y PCI.
     * @param rawResponse Cadena de respuesta del chip (ej: "7E8 04 41 0C 0B B8 \r\r>").
     * @param serviceHex Servicio esperado en hexadecimal (ej: "41" para Modo 01, "61" o "62").
     * @param pidHex Identificador PID o DID (ej: "0C", "82").
     * @return Arreglo de bytes con los datos útiles (A, B, C...).
     */
    static QByteArray extractDataBytes(const QString& rawResponse, const QString& serviceHex, const QString& pidHex);
};

