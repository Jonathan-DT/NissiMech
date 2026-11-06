#pragma once

#include <QString>
#include <QByteArray>
#include <optional>
#include <cstdint>
#include "SignalExtractor.h"

/**
 * @brief Decodificador dinámico de PIDs y señales vehiculares según estándares OBDb JSON v3.
 * Elimina toda fórmula rígida (hardcoded) en C++, derivando la decodificación estrictamente
 * de la definición de la señal cargada en tiempo de ejecución.
 */
class PidDecoder
{
public:
    PidDecoder() = delete;

    /**
     * @brief Decodifica dinámicamente el valor físico de una señal OBDb a partir de la respuesta en bruto.
     * @param rawResponse Cadena de respuesta del adaptador ELM327 / STN.
     * @param serviceHex Servicio esperado (ej. "41", "61", "62").
     * @param pidHex Identificador PID o DID (ej. "0C", "01", "1E 1C").
     * @param fmt Estructura de escalado de bits y offsets de la señal en OBDb.
     * @return Valor físico en unidades de ingeniería, o std::nullopt si no coincide o no es válido.
     */
    static std::optional<double> decodeObdbSignal(const QString& rawResponse,
                                                  const QString& serviceHex,
                                                  const QString& pidHex,
                                                  const ObdbSignalFormat& fmt);

    /**
     * @brief Extrae los bytes de datos útiles de una respuesta bruta ELM327 filtrando headers y PCI.
     * @param rawResponse Cadena de respuesta del chip (ej: "7E8 04 41 0C 0B B8 \r\r>").
     * @param serviceHex Servicio esperado en hexadecimal (ej: "41", "61", "62").
     * @param pidHex Identificador PID o DID (ej: "0C", "82", "1E 1C").
     * @return Arreglo de bytes con los datos útiles (A, B, C...).
     */
    static QByteArray extractDataBytes(const QString& rawResponse, const QString& serviceHex, const QString& pidHex);
};
