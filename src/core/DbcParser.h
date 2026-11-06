#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QSet>
#include <cstdint>
#include <optional>
#include "SignalExtractor.h"

struct DbcSignal {
    QString name;
    DbcSignalFormat fmt;
    QString receiver;
};

struct DbcMessage {
    uint32_t idDecimal{0};
    QString idHex;          // 3 dígitos para CAN standard (11-bit), ej. "0AA", o 8 para extended
    QString name;
    int dlc{8};
    QString transmitter;
    QList<DbcSignal> signalList;
};

/**
 * @brief Parser nativo de archivos Vector CAN .dbc (estándar commaai/opendbc).
 * Implementado en C++20 sin dependencias externas (solo STL y Qt6::Core QRegularExpression).
 */
class DbcParser
{
public:
    DbcParser() = default;

    /**
     * @brief Carga y parsea un archivo .dbc desde disco.
     */
    bool loadFile(const QString& filePath);

    /**
     * @brief Parsea contenido textual en formato Vector DBC.
     */
    bool parseContent(const QString& dbcContent);

    void clear();
    bool isLoaded() const { return !m_messagesById.isEmpty(); }

    const DbcMessage* findMessageById(uint32_t id) const;
    const DbcMessage* findMessageByHex(const QString& idHex) const;
    const DbcMessage* findMessageByName(const QString& name) const;

    /**
     * @brief Decodifica una señal específica de un frame CAN de 8 bytes recibido.
     */
    std::optional<double> decodeSignal(uint32_t messageId, const QString& signalName, const QByteArray& payload) const;

    const QList<DbcMessage>& messages() const { return m_messages; }

    /**
     * @brief Retorna el conjunto de IDs CAN en hexadecimal (ej. "0AA", "170", "0C9") para configurar filtros AT CRA.
     */
    QSet<QString> authorizedHexIds() const;

private:
    QList<DbcMessage> m_messages;
    QMap<uint32_t, int> m_messagesById;       // idDecimal -> index in m_messages
    QMap<QString, int> m_messagesByName;     // messageName -> index in m_messages
    QMap<QString, int> m_messagesByHex;      // idHex -> index in m_messages
};
