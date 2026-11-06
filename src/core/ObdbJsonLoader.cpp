#include "ObdbJsonLoader.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

static QString resolveObdbPath(const QString& filePath)
{
    if (QFileInfo::exists(filePath)) {
        return filePath;
    }

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
            return cand;
        }
    }
    return filePath;
}

bool ObdbJsonLoader::loadFile(const QString& filePath, std::optional<int> modelYear)
{
    clear();
    return appendFile(filePath, modelYear);
}

bool ObdbJsonLoader::appendFile(const QString& filePath, std::optional<int> modelYear)
{
    QString targetPath = resolveObdbPath(filePath);
    QFile file(targetPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "No se pudo abrir el archivo OBDb:" << filePath << "ni en rutas relativas probadas";
        return false;
    }
    return loadJson(file.readAll(), modelYear, false);
}

bool ObdbJsonLoader::loadJson(const QByteArray& jsonData, std::optional<int> modelYear, bool clearExisting)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Error parseando OBDb JSON:" << err.errorString();
        return false;
    }

    if (clearExisting) {
        clear();
    }

    QJsonObject root = doc.object();

    // 1. ECUs
    if (root.contains(QStringLiteral("ecu")) && root.value(QStringLiteral("ecu")).isArray()) {
        QJsonArray ecuArr = root.value(QStringLiteral("ecu")).toArray();
        for (const auto& val : ecuArr) {
            if (!val.isObject()) continue;
            QJsonObject ecuObj = val.toObject();
            ObdbEcu ecu;
            ecu.hdr = ecuObj.value(QStringLiteral("hdr")).toString().trimmed().toUpper();
            ecu.type = ecuObj.value(QStringLiteral("type")).toString().trimmed();
            if (!ecu.hdr.isEmpty()) {
                bool alreadyExists = false;
                for (const auto& existing : m_ecus) {
                    if (existing.hdr == ecu.hdr) {
                        alreadyExists = true;
                        break;
                    }
                }
                if (!alreadyExists) {
                    m_ecus.append(ecu);
                }
            }
        }
    }

    // 2. Commands
    if (root.contains(QStringLiteral("commands")) && root.value(QStringLiteral("commands")).isArray()) {
        QJsonArray cmdArr = root.value(QStringLiteral("commands")).toArray();
        for (const auto& val : cmdArr) {
            if (!val.isObject()) continue;
            QJsonObject cmdObj = val.toObject();

            // Filtrado por año modelo según "filter"
            if (modelYear.has_value() && cmdObj.contains(QStringLiteral("filter")) && cmdObj.value(QStringLiteral("filter")).isObject()) {
                QJsonObject filt = cmdObj.value(QStringLiteral("filter")).toObject();
                if (filt.contains(QStringLiteral("from"))) {
                    int fromYear = filt.value(QStringLiteral("from")).toInt();
                    if (*modelYear < fromYear) {
                        continue;
                    }
                }
                if (filt.contains(QStringLiteral("to"))) {
                    int toYear = filt.value(QStringLiteral("to")).toInt();
                    if (*modelYear > toYear) {
                        continue;
                    }
                }
            }

            ObdbCommand cmd;
            cmd.hdr = cmdObj.value(QStringLiteral("hdr")).toString().trimmed().toUpper();
            cmd.rax = cmdObj.value(QStringLiteral("rax")).toString().trimmed().toUpper();
            cmd.freq = cmdObj.value(QStringLiteral("freq")).toDouble(0.0);

            // Objeto "cmd" (ej. {"22": "1E1C"} o {"01": "0C"})
            if (cmdObj.contains(QStringLiteral("cmd")) && cmdObj.value(QStringLiteral("cmd")).isObject()) {
                QJsonObject cmdInner = cmdObj.value(QStringLiteral("cmd")).toObject();
                QStringList keys = cmdInner.keys();
                if (!keys.isEmpty()) {
                    cmd.service = keys.first().trimmed().toUpper();
                    cmd.pidHex = cmdInner.value(cmd.service).toString().trimmed().toUpper();

                    // Formatear pidHex con espacios si son múltiples bytes (ej. 1E1C -> 1E 1C)
                    QString formattedPid = cmd.pidHex;
                    if (formattedPid.length() > 2 && !formattedPid.contains(QLatin1Char(' '))) {
                        QString spaced;
                        for (int i = 0; i < formattedPid.length(); i += 2) {
                            if (i > 0) spaced.append(QLatin1Char(' '));
                            spaced.append(formattedPid.mid(i, 2));
                        }
                        formattedPid = spaced;
                    }
                    cmd.fullCmdHex = QStringLiteral("%1 %2").arg(cmd.service, formattedPid).trimmed();
                }
            }

            // Señales
            if (cmdObj.contains(QStringLiteral("signals")) && cmdObj.value(QStringLiteral("signals")).isArray()) {
                QJsonArray sigArr = cmdObj.value(QStringLiteral("signals")).toArray();
                for (const auto& sigVal : sigArr) {
                    if (!sigVal.isObject()) continue;
                    QJsonObject sigObj = sigVal.toObject();

                    ObdbSignal sig;
                    sig.id = sigObj.value(QStringLiteral("id")).toString().trimmed();
                    sig.name = sigObj.value(QStringLiteral("name")).toString().trimmed();
                    sig.path = sigObj.value(QStringLiteral("path")).toString().trimmed();
                    sig.suggestedMetric = sigObj.value(QStringLiteral("suggestedMetric")).toString().trimmed();

                    if (sigObj.contains(QStringLiteral("fmt")) && sigObj.value(QStringLiteral("fmt")).isObject()) {
                        QJsonObject fmtObj = sigObj.value(QStringLiteral("fmt")).toObject();
                        sig.fmt.bix = fmtObj.value(QStringLiteral("bix")).toInt(0);
                        sig.fmt.len = fmtObj.value(QStringLiteral("len")).toInt(8);
                        // Soporta tanto "sign": true como "signed": true
                        sig.fmt.isSigned = fmtObj.value(QStringLiteral("sign")).toBool(fmtObj.value(QStringLiteral("signed")).toBool(false));
                        sig.fmt.mul = fmtObj.value(QStringLiteral("mul")).toDouble(1.0);
                        sig.fmt.div = fmtObj.value(QStringLiteral("div")).toDouble(1.0);
                        sig.fmt.add = fmtObj.value(QStringLiteral("add")).toDouble(0.0);
                        sig.fmt.minVal = fmtObj.value(QStringLiteral("min")).toDouble(0.0);
                        sig.fmt.maxVal = fmtObj.value(QStringLiteral("max")).toDouble(0.0);
                        sig.fmt.unit = fmtObj.value(QStringLiteral("unit")).toString().trimmed();
                    }

                    if (!sig.id.isEmpty()) {
                        cmd.signalList.append(sig);
                        m_signalsById.insert(sig.id, sig);
                    }
                }
            }

            int cmdIdx = m_commands.size();
            m_commands.append(cmd);
            for (const auto& s : cmd.signalList) {
                m_signalIdToCmdIndex.insert(s.id, cmdIdx);
            }
        }
    }

    return true;
}

void ObdbJsonLoader::clear()
{
    m_ecus.clear();
    m_commands.clear();
    m_signalIdToCmdIndex.clear();
    m_signalsById.clear();
}

QStringList ObdbJsonLoader::availableSignalIds() const
{
    return m_signalsById.keys();
}

const ObdbSignal* ObdbJsonLoader::findSignal(const QString& signalId) const
{
    auto it = m_signalsById.find(signalId);
    if (it != m_signalsById.end()) {
        return &it.value();
    }
    return nullptr;
}

const ObdbCommand* ObdbJsonLoader::findCommandForSignal(const QString& signalId) const
{
    auto it = m_signalIdToCmdIndex.find(signalId);
    if (it != m_signalIdToCmdIndex.end() && it.value() >= 0 && it.value() < m_commands.size()) {
        return &m_commands.at(it.value());
    }
    return nullptr;
}

const ObdbCommand* ObdbJsonLoader::findCommand(const QString& service, const QString& pidHex, const QString& hdr) const
{
    QString sClean = service.trimmed().toUpper();
    QString pClean = pidHex.trimmed().toUpper();
    pClean.remove(QLatin1Char(' '));
    QString hClean = hdr.trimmed().toUpper();

    for (const auto& cmd : m_commands) {
        if (!hClean.isEmpty() && !cmd.hdr.isEmpty() && cmd.hdr != hClean) {
            continue;
        }
        if (cmd.service == sClean) {
            QString cmdPid = cmd.pidHex;
            cmdPid.remove(QLatin1Char(' '));
            if (cmdPid == pClean) {
                return &cmd;
            }
        }
    }
    return nullptr;
}

QSet<QString> ObdbJsonLoader::authorizedHeaders() const
{
    QSet<QString> result;
    for (const auto& ecu : m_ecus) {
        if (!ecu.hdr.isEmpty()) result.insert(ecu.hdr);
    }
    for (const auto& cmd : m_commands) {
        if (!cmd.hdr.isEmpty()) result.insert(cmd.hdr);
    }
    return result;
}

QSet<QString> ObdbJsonLoader::authorizedCraFilters() const
{
    QSet<QString> result;
    for (const auto& cmd : m_commands) {
        if (!cmd.rax.isEmpty()) result.insert(cmd.rax);
    }
    return result;
}

QSet<QString> ObdbJsonLoader::authorizedCommands() const
{
    QSet<QString> result;
    for (const auto& cmd : m_commands) {
        if (!cmd.fullCmdHex.isEmpty()) {
            result.insert(cmd.fullCmdHex);
            QString compact = cmd.fullCmdHex;
            compact.remove(QLatin1Char(' '));
            result.insert(compact);
        }
    }
    return result;
}
