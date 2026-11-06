#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QSet>
#include <optional>
#include "SignalExtractor.h"

struct ObdbSignal {
    QString id;
    QString name;
    QString path;
    QString suggestedMetric;
    ObdbSignalFormat fmt;
};

struct ObdbCommand {
    QString hdr;            // Header transmisión (ej. "7DF", "7E0", "700") -> AT SH <hdr>
    QString rax;            // Header recepción (ej. "7E8", "708") -> AT CRA <rax>
    QString service;        // Servicio hex (ej. "01", "21", "22", "09")
    QString pidHex;         // PID / DID hex (ej. "0C", "01", "1E1C", "02")
    QString fullCmdHex;     // Comando completo (ej. "01 0C", "21 01", "22 1E 1C")
    double freq{0.0};
    QList<ObdbSignal> signalList;
};

struct ObdbEcu {
    QString hdr;
    QString type;
};

/**
 * @brief Cargador y gestor nativo de archivos de señales en estándar OBDb JSON v3.
 * Utiliza QJsonDocument y QJsonObject (LGPLv3 Qt6::Core) sin dependencias externas.
 */
class ObdbJsonLoader
{
public:
    ObdbJsonLoader() = default;

    /**
     * @brief Carga y parsea un archivo de señales OBDb JSON v3.
     */
    bool loadFile(const QString& filePath);

    /**
     * @brief Carga y parsea contenido JSON en memoria.
     */
    bool loadJson(const QByteArray& jsonData);

    /**
     * @brief Limpia todas las definiciones cargadas.
     */
    void clear();

    bool isLoaded() const { return !m_commands.isEmpty() || !m_ecus.isEmpty(); }

    QStringList availableSignalIds() const;
    const ObdbSignal* findSignal(const QString& signalId) const;
    const ObdbCommand* findCommandForSignal(const QString& signalId) const;
    const ObdbCommand* findCommand(const QString& service, const QString& pidHex, const QString& hdr = QString()) const;

    const QList<ObdbCommand>& commands() const { return m_commands; }
    const QList<ObdbEcu>& ecus() const { return m_ecus; }

    QSet<QString> authorizedHeaders() const;
    QSet<QString> authorizedCraFilters() const;
    QSet<QString> authorizedCommands() const;

private:
    QList<ObdbEcu> m_ecus;
    QList<ObdbCommand> m_commands;
    QMap<QString, int> m_signalIdToCmdIndex; // signalId -> index in m_commands
    QMap<QString, ObdbSignal> m_signalsById;
};
