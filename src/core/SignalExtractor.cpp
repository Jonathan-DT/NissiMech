#include "SignalExtractor.h"
#include <cmath>

std::optional<double> SignalExtractor::extractObdbValue(const QByteArray& payload, const ObdbSignalFormat& fmt)
{
    if (payload.isEmpty() || fmt.len <= 0 || fmt.len > 64) {
        return std::nullopt;
    }

    int totalBits = payload.size() * 8;
    if (fmt.bix + fmt.len > totalBits) {
        return std::nullopt;
    }

    const auto* data = reinterpret_cast<const uint8_t*>(payload.constData());
    uint64_t rawVal = 0;

    // Extracción secuencial MSB-first en OBDb
    for (int i = 0; i < fmt.len; ++i) {
        int bitIdx = fmt.bix + i;
        int byteIdx = bitIdx / 8;
        int bitInByte = 7 - (bitIdx % 8);
        uint8_t bit = (data[byteIdx] >> bitInByte) & 0x01;
        rawVal = (rawVal << 1) | bit;
    }

    double physical = 0.0;
    if (fmt.isSigned && fmt.len < 64) {
        uint64_t signMask = 1ULL << (fmt.len - 1);
        int64_t signedVal = 0;
        if (rawVal & signMask) {
            signedVal = static_cast<int64_t>(rawVal - (1ULL << fmt.len));
        } else {
            signedVal = static_cast<int64_t>(rawVal);
        }
        double div = (std::abs(fmt.div) < 1e-12) ? 1.0 : fmt.div;
        physical = ((static_cast<double>(signedVal) * fmt.mul) / div) + fmt.add;
    } else {
        double div = (std::abs(fmt.div) < 1e-12) ? 1.0 : fmt.div;
        physical = ((static_cast<double>(rawVal) * fmt.mul) / div) + fmt.add;
    }

    return physical;
}

uint64_t SignalExtractor::extractRawBitsDbc(const uint8_t* data, int dataLen, int startBit, int bitLength, bool isLittleEndian)
{
    if (!data || dataLen <= 0 || bitLength <= 0 || bitLength > 64) {
        return 0;
    }

    uint64_t rawVal = 0;

    if (isLittleEndian) {
        // Intel (@1): startBit es el LSB del campo
        for (int i = 0; i < bitLength; ++i) {
            int canBit = startBit + i;
            int byteIdx = canBit / 8;
            int bitInByte = canBit % 8;
            if (byteIdx < dataLen) {
                uint8_t bit = (data[byteIdx] >> bitInByte) & 0x01;
                rawVal |= (static_cast<uint64_t>(bit) << i);
            }
        }
    } else {
        // Motorola (@0): startBit es el MSB del campo
        int byteIdx = startBit / 8;
        int bitInByte = startBit % 8;

        for (int i = 0; i < bitLength; ++i) {
            if (byteIdx < dataLen) {
                uint8_t bit = (data[byteIdx] >> bitInByte) & 0x01;
                rawVal = (rawVal << 1) | bit;
            }

            if (bitInByte == 0) {
                bitInByte = 7;
                byteIdx++;
            } else {
                bitInByte--;
            }
        }
    }

    return rawVal;
}

std::optional<double> SignalExtractor::extractDbcValue(const QByteArray& canFramePayload, const DbcSignalFormat& fmt)
{
    if (canFramePayload.isEmpty() || fmt.bitLength <= 0 || fmt.bitLength > 64) {
        return std::nullopt;
    }

    const auto* data = reinterpret_cast<const uint8_t*>(canFramePayload.constData());
    uint64_t rawVal = extractRawBitsDbc(data, canFramePayload.size(), fmt.startBit, fmt.bitLength, fmt.isLittleEndian);

    double physical = 0.0;
    if (fmt.isSigned && fmt.bitLength < 64) {
        uint64_t signMask = 1ULL << (fmt.bitLength - 1);
        int64_t signedVal = 0;
        if (rawVal & signMask) {
            signedVal = static_cast<int64_t>(rawVal - (1ULL << fmt.bitLength));
        } else {
            signedVal = static_cast<int64_t>(rawVal);
        }
        physical = (static_cast<double>(signedVal) * fmt.scale) + fmt.offset;
    } else {
        physical = (static_cast<double>(rawVal) * fmt.scale) + fmt.offset;
    }

    return physical;
}

