#pragma once

#include <QString>
#include <QByteArray>
#include <cstdint>
#include <optional>

/**
 * @brief Formato de señal según especificación OBDb JSON v3.
 */
struct ObdbSignalFormat {
    int bix{0};              // Índice inicial de bit (0-indexed)
    int len{8};              // Longitud en bits
    bool isSigned{false};    // Con o sin signo
    double mul{1.0};         // Multiplicador
    double div{1.0};         // Divisor
    double add{0.0};         // Desplazamiento (offset)
    double minVal{0.0};
    double maxVal{0.0};
    QString unit;
};

/**
 * @brief Formato de señal Vector CAN DBC según especificación commaai/opendbc.
 */
struct DbcSignalFormat {
    int startBit{0};         // Bit de inicio (MSB para Motorola @0, LSB para Intel @1)
    int bitLength{8};        // Longitud en bits
    bool isLittleEndian{false}; // true = Intel (@1), false = Motorola (@0)
    bool isSigned{false};    // '+' = unsigned, '-' = signed
    double scale{1.0};       // Factor de escala (SCALE)
    double offset{0.0};      // Desplazamiento (OFFSET)
    double minVal{0.0};
    double maxVal{0.0};
    QString unit;
};

/**
 * @brief Extractor universal a nivel de bits para protocolos OBDb y CAN DBC.
 */
class SignalExtractor
{
public:
    SignalExtractor() = delete;

    /**
     * @brief Extrae y escala una señal de un payload según OBDb JSON v3.
     * Fórmula: ((raw_bits * mul) / div) + add
     */
    static std::optional<double> extractObdbValue(const QByteArray& payload, const ObdbSignalFormat& fmt);

    /**
     * @brief Extrae y escala una señal de una trama CAN de 8 bytes según Vector DBC.
     * Fórmula: (raw_bits * scale) + offset
     */
    static std::optional<double> extractDbcValue(const QByteArray& canFramePayload, const DbcSignalFormat& fmt);

    /**
     * @brief Extrae el valor entero bruto (raw bits) de una trama CAN de hasta 8 bytes.
     */
    static uint64_t extractRawBitsDbc(const uint8_t* data, int dataLen, int startBit, int bitLength, bool isLittleEndian);
};
