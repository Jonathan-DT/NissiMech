#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QSqlDatabase>
#include <cstdint>

struct DtcInfo {
    QString code;
    QString system;
    QString descriptionEs;
    int severity{1};
    QString faultType;  // Subtipo de falla UDS (FTB - Fault Type Byte), ej. "00", "2F"
    uint8_t statusByte{0}; // Estado UDS (StatusOfDTC)

    bool isValid() const { return !code.isEmpty(); }
};

/**
 * @brief Gestor de Códigos de Diagnóstico de Falla (DTC).
 * 
 * Implementa la decodificación binaria estricta según la norma SAE J2012 / ISO 15031-6
 * y consulta de descripciones clínicas y severidad desde SQLite3 (QtSql).
 */
class DtcManager : public QObject
{
    Q_OBJECT

public:
    explicit DtcManager(QObject* parent = nullptr);
    ~DtcManager() override;

    /**
     * @brief Inicializa y conecta la base de datos SQLite.
     * @param dbPath Ruta al fichero .sqlite. Si está vacía, busca en rutas estándar relativas.
     */
    bool initializeDatabase(const QString& dbPath = QString());

    /**
     * @brief Decodifica un par de bytes en bruto según SAE J2012.
     * @param byteA Primer byte del par de falla (prefijo + dígitos 2 y 3).
     * @param byteB Segundo byte del par de falla (dígitos 4 y 5).
     * @return Código alfanumérico formateado (ej. "P0171", "C0035", "C0200", "U0100").
     */
    static QString decodeDtcPair(uint8_t byteA, uint8_t byteB);

    /**
     * @brief Consulta en la base de datos SQLite la información de un código DTC.
     */
    DtcInfo getDtcDetails(const QString& code);

    /**
     * @brief Parsea una respuesta completa de lectura de fallas (Modo 03, 07 o UDS 19 02).
     */
    QList<DtcInfo> parseDtcResponse(const QString& rawResponse);

private:
    void ensureDefaultSchema();

    QSqlDatabase m_db;
    bool m_dbInitialized{false};
};
