#include "MockElmDriver.h"
#include "../core/SafetyGuard.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

MockElmDriver::MockElmDriver(QObject* parent)
    : IObdInterface(parent)
{
}

bool MockElmDriver::openConnection(const QString& portName, int baudRate)
{
    Q_UNUSED(portName);
    Q_UNUSED(baudRate);

    // Si no se ha cargado escenario explícito, intentar cargar escenario por defecto
    if (m_scenarioResponses.isEmpty() && m_wildcardResponses.isEmpty()) {
        const QStringList defaultCandidates = {
            QStringLiteral("database/scenarios/toyota_camry_2006_test.json"),
            QStringLiteral("../database/scenarios/toyota_camry_2006_test.json"),
            QDir::current().filePath(QStringLiteral("database/scenarios/toyota_camry_2006_test.json")),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../database/scenarios/toyota_camry_2006_test.json"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/database/scenarios/toyota_camry_2006_test.json")
        };
        for (const QString& cand : defaultCandidates) {
            if (QFileInfo::exists(cand)) {
                loadScenario(cand);
                break;
            }
        }
    }

    m_connected = true;
    emit connectionStateChanged(true);
    return true;
}

void MockElmDriver::closeConnection()
{
    if (m_connected) {
        m_connected = false;
        emit connectionStateChanged(false);
    }
}

bool MockElmDriver::isConnected() const
{
    return m_connected;
}

void MockElmDriver::setCurrentHeader(const QString& header)
{
    m_currentHeader = header.trimmed().toUpper();
}

void MockElmDriver::setCurrentCra(const QString& cra)
{
    m_currentCra = cra.trimmed().toUpper();
}

void MockElmDriver::setResponse(const QString& command, const QString& response, const QString& header)
{
    QString normCmd = command.trimmed().toUpper();
    QString compactCmd = normCmd;
    compactCmd.remove(QLatin1Char(' '));
    QString normHdr = header.trimmed().toUpper();

    if (normHdr.isEmpty() || normHdr == QStringLiteral("*")) {
        m_wildcardResponses.insert(normCmd, response);
        m_wildcardResponses.insert(compactCmd, response);
    } else {
        m_scenarioResponses.insert(qMakePair(normHdr, normCmd), response);
        m_scenarioResponses.insert(qMakePair(normHdr, compactCmd), response);
    }
}

bool MockElmDriver::loadScenario(const QString& scenarioPath)
{
    QString targetPath = scenarioPath;
    if (!QFileInfo::exists(targetPath)) {
        const QStringList candidatePaths = {
            scenarioPath,
            QStringLiteral("database/scenarios/") + scenarioPath,
            QStringLiteral("../database/scenarios/") + scenarioPath,
            QDir::current().filePath(QStringLiteral("database/scenarios/") + scenarioPath),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../database/scenarios/") + scenarioPath,
            QCoreApplication::applicationDirPath() + QStringLiteral("/database/scenarios/") + scenarioPath
        };
        for (const QString& p : candidatePaths) {
            if (QFileInfo::exists(p)) {
                targetPath = p;
                break;
            }
        }
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "No se pudo abrir el archivo de escenario mock:" << targetPath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning() << "Formato inválido en escenario mock:" << targetPath;
        return false;
    }

    m_scenarioResponses.clear();
    m_wildcardResponses.clear();

    QJsonObject root = doc.object();
    m_scenarioName = root.value(QStringLiteral("scenario_name")).toString();
    m_vehicleFolder = root.value(QStringLiteral("vehicle_folder")).toString();

    QJsonArray respArr = root.value(QStringLiteral("responses")).toArray();
    for (const auto& itemVal : respArr) {
        if (!itemVal.isObject()) continue;
        QJsonObject obj = itemVal.toObject();
        QString header = obj.value(QStringLiteral("header")).toString().trimmed().toUpper();
        QString command = obj.value(QStringLiteral("command")).toString().trimmed().toUpper();
        QString response = obj.value(QStringLiteral("response")).toString();

        if (header.isEmpty()) header = QStringLiteral("*");

        if (header == QStringLiteral("*")) {
            m_wildcardResponses.insert(command, response);
            QString compact = command;
            compact.remove(QLatin1Char(' '));
            m_wildcardResponses.insert(compact, response);
        } else {
            m_scenarioResponses.insert(qMakePair(header, command), response);
            QString compact = command;
            compact.remove(QLatin1Char(' '));
            m_scenarioResponses.insert(qMakePair(header, compact), response);
        }
    }

    return true;
}

QString MockElmDriver::lookupResponse(const QString& cmd)
{
    QString normalized = cmd.trimmed().toUpper();
    QString compact = normalized;
    compact.remove(QLatin1Char(' '));

    // 1. Coincidencia específica por cabecera activa
    auto itSpecific = m_scenarioResponses.find(qMakePair(m_currentHeader, normalized));
    if (itSpecific != m_scenarioResponses.end()) {
        return itSpecific.value();
    }
    itSpecific = m_scenarioResponses.find(qMakePair(m_currentHeader, compact));
    if (itSpecific != m_scenarioResponses.end()) {
        return itSpecific.value();
    }

    // 2. Coincidencia comodín (header "*")
    auto itWild = m_wildcardResponses.find(normalized);
    if (itWild != m_wildcardResponses.end()) {
        return itWild.value();
    }
    itWild = m_wildcardResponses.find(compact);
    if (itWild != m_wildcardResponses.end()) {
        return itWild.value();
    }

    // Si no está registrado en el escenario, respuesta oficial ELM327
    return QStringLiteral("NO DATA\r\r>");
}

QString MockElmDriver::sendRawCommand(const QString& cmd, int timeoutMs)
{
    Q_UNUSED(timeoutMs);

    if (!m_connected) {
        emit errorOccurred(QStringLiteral("Error: El simulador OBD no está conectado."));
        return QString();
    }

    // Validación estricta por el cortafuegos de seguridad de ECU
    QString blockReason;
    if (!SafetyGuard::isCommandSafe(cmd, SafetyGuard::isUserConfirmedClearDtc(), &blockReason)) {
        emit errorOccurred(QStringLiteral("Comando bloqueado por SafetyGuard: %1 (%2)").arg(cmd, blockReason));
        return QString();
    }

    QString normalized = cmd.trimmed().toUpper();

    // Actualización de cabeceras AT SH y filtros AT CRA
    if (normalized.startsWith(QStringLiteral("AT SH"))) {
        m_currentHeader = normalized.mid(5).trimmed();
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized.startsWith(QStringLiteral("AT CRA"))) {
        m_currentCra = normalized.mid(6).trimmed();
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT H1")) {
        m_headersEnabled = true;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT H0")) {
        m_headersEnabled = false;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT E0")) {
        m_echoEnabled = false;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }
    if (normalized == QStringLiteral("AT E1")) {
        m_echoEnabled = true;
        QString resp = QStringLiteral("OK\r\r>");
        emit rawDataReceived(resp);
        return resp;
    }

    QString response = lookupResponse(normalized);
    emit rawDataReceived(response);
    return response;
}
