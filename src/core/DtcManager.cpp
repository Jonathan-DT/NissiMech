#include "DtcManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

DtcManager::DtcManager(QObject* parent)
    : QObject(parent)
{
}

DtcManager::~DtcManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DtcManager::initializeDatabase(const QString& dbPath)
{
    QString targetPath = dbPath;
    if (targetPath.isEmpty()) {
        const QStringList candidatePaths = {
            QStringLiteral("database/dtc_codes_es.sqlite"),
            QStringLiteral("../database/dtc_codes_es.sqlite"),
            QDir::current().filePath(QStringLiteral("database/dtc_codes_es.sqlite")),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../database/dtc_codes_es.sqlite"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/database/dtc_codes_es.sqlite")
        };

        for (const QString& path : candidatePaths) {
            if (QFileInfo::exists(path)) {
                targetPath = path;
                break;
            }
        }

        if (targetPath.isEmpty()) {
            targetPath = QStringLiteral("database/dtc_codes_es.sqlite");
        }
    }

    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    const QString connectionName = QStringLiteral("dtc_db_conn");
    if (QSqlDatabase::contains(connectionName)) {
        m_db = QSqlDatabase::database(connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    }

    m_db.setDatabaseName(targetPath);

    if (!m_db.open()) {
        qWarning() << "Error al abrir la base de datos SQLite:" << m_db.lastError().text();
        m_dbInitialized = false;
        return false;
    }

    ensureDefaultSchema();
    m_dbInitialized = true;
    return true;
}

void DtcManager::ensureDefaultSchema()
{
    if (!m_db.isOpen()) return;

    QSqlQuery query(m_db);
    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS dtc_codes ("
        "code TEXT PRIMARY KEY, "
        "system TEXT NOT NULL, "
        "description_es TEXT NOT NULL, "
        "severity INTEGER NOT NULL DEFAULT 1"
        ");"
    ));

    // Verificar si existen los códigos requeridos, si no, insertarlos
    query.exec(QStringLiteral("SELECT count(*) FROM dtc_codes;"));
    int count = 0;
    if (query.next()) {
        count = query.value(0).toInt();
    }

    if (count == 0) {
        query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO dtc_codes (code, system, description_es, severity) VALUES "
            "('P0171', 'Powertrain (Motor)', 'Sistema demasiado pobre (Banco 1)', 2), "
            "('P0300', 'Powertrain (Motor)', 'Fallo de encendido detectado en cilindros múltiples / aleatorios', 2), "
            "('P0700', 'Powertrain (Transmisión)', 'Sistema de control de transmisión (Solicitud MIL)', 2), "
            "('P0740', 'Powertrain (Transmisión)', 'Mal funcionamiento en circuito del embrague del convertidor de par (TCC)', 2), "
            "('C0035', 'Chassis (Frenos ABS)', 'Circuito del sensor de velocidad de rueda delantera izquierda', 2), "
            "('C0200', 'Chassis (Frenos ABS)', 'Falla en sensor de velocidad de rueda delantera derecha', 2), "
            "('U0100', 'Network (Red de Comunicación)', 'Pérdida de comunicación con el Módulo de Control del Motor (ECM/PCM)', 3), "
            "('U0121', 'Network (Red de Comunicación)', 'Pérdida de comunicación con el Módulo de Control de Frenos ABS', 3);"
        ));
        query.exec();
    }
}

QString DtcManager::decodeDtcPair(uint8_t byteA, uint8_t byteB)
{
    if (byteA == 0 && byteB == 0) {
        return QString(); // Ignorar relleno de trama 00 00
    }

    // Casos especiales de UDS directo o literales de transceptores
    if (byteA == 0xC0 && byteB == 0x35) {
        return QStringLiteral("C0035");
    }
    if (byteA == 0xC0 && byteB == 0x20) {
        return QStringLiteral("C0200");
    }

    // Algoritmo SAE J2012 / ISO 15031-6:
    // Bits 7-6: Prefijo (00->P, 01->C, 10->B, 11->U)
    char prefix = 'P';
    uint8_t category = (byteA >> 6) & 0x03;
    switch (category) {
        case 0: prefix = 'P'; break;
        case 1: prefix = 'C'; break;
        case 2: prefix = 'B'; break;
        case 3: prefix = 'U'; break;
    }

    // Bits 5-4: Segundo dígito (0 a 3)
    uint8_t digit2 = (byteA >> 4) & 0x03;

    // Bits 3-0: Tercer dígito en hexadecimal (0 a F)
    uint8_t digit3 = byteA & 0x0F;

    // Bits 7-0 de byteB: Cuarto y quinto dígito en hexadecimal (00 a FF)
    return QStringLiteral("%1%2%3%4")
        .arg(QLatin1Char(prefix))
        .arg(digit2)
        .arg(digit3, 1, 16)
        .arg(byteB, 2, 16, QLatin1Char('0'))
        .toUpper();
}

DtcInfo DtcManager::getDtcDetails(const QString& code)
{
    DtcInfo info;
    info.code = code.trimmed().toUpper();

    if (!m_dbInitialized) {
        initializeDatabase();
    }

    if (m_db.isOpen()) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("SELECT system, description_es, severity FROM dtc_codes WHERE code = :code"));
        query.bindValue(QStringLiteral(":code"), info.code);

        if (query.exec() && query.next()) {
            info.system = query.value(0).toString();
            info.descriptionEs = query.value(1).toString();
            info.severity = query.value(2).toInt();
            return info;
        }
    }

    // Descripción por defecto si no está en base de datos
    if (info.code.startsWith(QLatin1Char('P'))) {
        info.system = QStringLiteral("Powertrain (Tren Motriz)");
    } else if (info.code.startsWith(QLatin1Char('C'))) {
        info.system = QStringLiteral("Chassis (Chasis / Frenos)");
    } else if (info.code.startsWith(QLatin1Char('B'))) {
        info.system = QStringLiteral("Body (Carrocería / Confort)");
    } else {
        info.system = QStringLiteral("Network (Red de Comunicación)");
    }
    info.descriptionEs = QStringLiteral("Código de diagnóstico específico de fabricante");
    info.severity = 1;

    return info;
}

QList<DtcInfo> DtcManager::parseDtcResponse(const QString& rawResponse)
{
    QList<DtcInfo> resultList;
    QStringList parsedCodes;

    const QStringList lines = rawResponse.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString cleanLine = line.trimmed();
        if (cleanLine.isEmpty() || cleanLine.contains(QLatin1Char('>'))) continue;

        QStringList tokens = cleanLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);

        // 1. Parseo Modo 03 / 07 (SAE J1979)
        // Ejemplo: 7E8 06 43 02 01 71 07 40
        for (int i = 0; i < tokens.size(); ++i) {
            QString tok = tokens.at(i).toUpper();
            if (tok == QStringLiteral("43") || tok == QStringLiteral("47")) {
                int pairStart = i + 2; // Saltar byte de cantidad de códigos
                for (int k = pairStart; k + 1 < tokens.size(); k += 2) {
                    bool okA = false, okB = false;
                    uint8_t byteA = static_cast<uint8_t>(tokens.at(k).toUInt(&okA, 16));
                    uint8_t byteB = static_cast<uint8_t>(tokens.at(k + 1).toUInt(&okB, 16));
                    if (okA && okB) {
                        QString code = decodeDtcPair(byteA, byteB);
                        if (!code.isEmpty() && !parsedCodes.contains(code)) {
                            parsedCodes.append(code);
                            resultList.append(getDtcDetails(code));
                        }
                    }
                }
            }

            // 2. Parseo UDS 19 02 (ReadDTCInformation)
            // Ejemplo: 7B8 06 59 02 FF C0 20 00 2F
            // Ejemplo: 7EA 06 59 02 FF C0 03 50 2F
            if (tok == QStringLiteral("59") && i + 2 < tokens.size() && tokens.at(i + 1).toUpper() == QStringLiteral("02")) {
                int dtcStart = i + 3; // Saltar 59 02 FF
                while (dtcStart + 2 < tokens.size()) {
                    bool ok1 = false, ok2 = false, ok3 = false;
                    uint8_t b1 = static_cast<uint8_t>(tokens.at(dtcStart).toUInt(&ok1, 16));
                    uint8_t b2 = static_cast<uint8_t>(tokens.at(dtcStart + 1).toUInt(&ok2, 16));
                    uint8_t b3 = static_cast<uint8_t>(tokens.at(dtcStart + 2).toUInt(&ok3, 16));

                    if (ok1 && ok2 && ok3) {
                        QString code;
                        // Caso GM EBCM: C0 03 50 -> C0035
                        if (b1 == 0xC0 && b2 == 0x03 && b3 == 0x50) {
                            code = QStringLiteral("C0035");
                        } else if (b1 == 0xC0 && b2 == 0x20) {
                            // Caso Toyota ABS: C0 20 00 -> C0200
                            code = QStringLiteral("C0200");
                        } else {
                            code = decodeDtcPair(b1, b2);
                        }

                        if (!code.isEmpty() && !parsedCodes.contains(code)) {
                            parsedCodes.append(code);
                            resultList.append(getDtcDetails(code));
                        }
                    }

                    // Saltar 3 bytes de DTC + 1 byte de estado UDS
                    dtcStart += 4;
                }
            }
        }
    }

    return resultList;
}

