#include "DbcParser.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

bool DbcParser::loadFile(const QString& filePath)
{
    QString targetPath = filePath;
    if (!QFileInfo::exists(targetPath)) {
        const QStringList candidates = {
            QDir::current().filePath(filePath),
            QDir::current().filePath(QStringLiteral("../") + filePath),
            QDir::current().filePath(QStringLiteral("../../") + filePath),
            QCoreApplication::applicationDirPath() + QStringLiteral("/") + filePath,
            QCoreApplication::applicationDirPath() + QStringLiteral("/../") + filePath,
            QCoreApplication::applicationDirPath() + QStringLiteral("/../../") + filePath
        };
        for (const QString& cand : candidates) {
            if (QFileInfo::exists(cand)) {
                targetPath = cand;
                break;
            }
        }
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "No se pudo abrir el archivo DBC:" << filePath << "ni en rutas relativas probadas";
        return false;
    }
    QTextStream in(&file);
    return parseContent(in.readAll());
}

bool DbcParser::parseContent(const QString& dbcContent)
{
    clear();

    // Regex para definición de mensaje: BO_ <ID> <NAME>: <DLC> <TRANSMITTER>
    static const QRegularExpression boRegex(
        QString::fromUtf8(R"DBC(^BO_\s+(\d+)\s+([A-Za-z0-9_]+)\s*:\s*(\d+)\s+([A-Za-z0-9_]+))DBC")
    );

    // Regex para definición de señal: SG_ <NAME> : <START>|<LEN>@<ENDIAN><SIGN> (<SCALE>,<OFFSET>) [<MIN>|<MAX>] "<UNIT>" <RECEIVER>
    static const QRegularExpression sgRegex(
        QString::fromUtf8(R"DBC(^\s*SG_\s+([A-Za-z0-9_]+)\s*(?:[Mm\d]+)?\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(([-0-9.eE+]+),([-0-9.eE+]+)\)\s*\[([-0-9.eE+]+)\|([-0-9.eE+]+)\]\s*"([^"]*)"\s*(.*)$)DBC")
    );

    const QStringList lines = dbcContent.split(QLatin1Char('\n'));
    DbcMessage currentMsg;
    bool hasCurrentMsg = false;

    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("CM_")) || line.startsWith(QStringLiteral("//"))) {
            continue;
        }

        auto boMatch = boRegex.match(line);
        if (boMatch.hasMatch()) {
            if (hasCurrentMsg) {
                int idx = m_messages.size();
                m_messages.append(currentMsg);
                m_messagesById.insert(currentMsg.idDecimal, idx);
                m_messagesByName.insert(currentMsg.name, idx);
                m_messagesByHex.insert(currentMsg.idHex, idx);
            }

            currentMsg = DbcMessage();
            uint32_t rawId = boMatch.captured(1).toUInt();
            currentMsg.idDecimal = rawId & 0x1FFFFFFF;
            if (rawId > 0x7FF) {
                currentMsg.idHex = QStringLiteral("%1").arg(currentMsg.idDecimal, 8, 16, QLatin1Char('0')).toUpper();
            } else {
                currentMsg.idHex = QStringLiteral("%1").arg(currentMsg.idDecimal, 3, 16, QLatin1Char('0')).toUpper();
            }

            currentMsg.name = boMatch.captured(2).trimmed();
            currentMsg.dlc = boMatch.captured(3).toInt();
            currentMsg.transmitter = boMatch.captured(4).trimmed();
            hasCurrentMsg = true;
            continue;
        }

        if (hasCurrentMsg) {
            auto sgMatch = sgRegex.match(line);
            if (sgMatch.hasMatch()) {
                DbcSignal sig;
                sig.name = sgMatch.captured(1).trimmed();
                sig.fmt.startBit = sgMatch.captured(2).toInt();
                sig.fmt.bitLength = sgMatch.captured(3).toInt();
                sig.fmt.isLittleEndian = (sgMatch.captured(4) == QStringLiteral("1"));
                sig.fmt.isSigned = (sgMatch.captured(5) == QStringLiteral("-"));
                sig.fmt.scale = sgMatch.captured(6).toDouble();
                sig.fmt.offset = sgMatch.captured(7).toDouble();
                sig.fmt.minVal = sgMatch.captured(8).toDouble();
                sig.fmt.maxVal = sgMatch.captured(9).toDouble();
                sig.fmt.unit = sgMatch.captured(10).trimmed();
                sig.receiver = sgMatch.captured(11).trimmed();

                currentMsg.signalList.append(sig);
            }
        }
    }

    if (hasCurrentMsg) {
        int idx = m_messages.size();
        m_messages.append(currentMsg);
        m_messagesById.insert(currentMsg.idDecimal, idx);
        m_messagesByName.insert(currentMsg.name, idx);
        m_messagesByHex.insert(currentMsg.idHex, idx);
    }

    return !m_messages.isEmpty();
}

void DbcParser::clear()
{
    m_messages.clear();
    m_messagesById.clear();
    m_messagesByName.clear();
    m_messagesByHex.clear();
}

const DbcMessage* DbcParser::findMessageById(uint32_t id) const
{
    auto it = m_messagesById.find(id);
    if (it != m_messagesById.end() && it.value() >= 0 && it.value() < m_messages.size()) {
        return &m_messages.at(it.value());
    }
    return nullptr;
}

const DbcMessage* DbcParser::findMessageByHex(const QString& idHex) const
{
    QString clean = idHex.trimmed().toUpper();
    auto it = m_messagesByHex.find(clean);
    if (it != m_messagesByHex.end()) {
        return &m_messages.at(it.value());
    }

    bool ok = false;
    uint32_t idDec = clean.toUInt(&ok, 16);
    if (ok) {
        return findMessageById(idDec);
    }
    return nullptr;
}

const DbcMessage* DbcParser::findMessageByName(const QString& name) const
{
    auto it = m_messagesByName.find(name.trimmed());
    if (it != m_messagesByName.end() && it.value() >= 0 && it.value() < m_messages.size()) {
        return &m_messages.at(it.value());
    }
    return nullptr;
}

std::optional<double> DbcParser::decodeSignal(uint32_t messageId, const QString& signalName, const QByteArray& payload) const
{
    const DbcMessage* msg = findMessageById(messageId);
    if (!msg) return std::nullopt;

    for (const auto& sig : msg->signalList) {
        if (sig.name == signalName) {
            return SignalExtractor::extractDbcValue(payload, sig.fmt);
        }
    }
    return std::nullopt;
}

QSet<QString> DbcParser::authorizedHexIds() const
{
    QSet<QString> result;
    for (const auto& msg : m_messages) {
        if (!msg.idHex.isEmpty()) {
            result.insert(msg.idHex);
            bool ok = false;
            uint val = msg.idHex.toUInt(&ok, 16);
            if (ok) {
                result.insert(QString::number(val, 16).toUpper());
            }
        }
    }
    return result;
}
