#include "SafetyGuard.h"
#include <QRegularExpression>
#include <QStringList>

void SafetyGuard::setAuthorizedContext(const QSet<QString>& allowedCommands,
                                     const QSet<QString>& allowedHeaders,
                                     const QSet<QString>& allowedCraFilters)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_allowedCommands.clear();
    for (const auto& cmd : allowedCommands) {
        QString norm = cmd.trimmed().toUpper();
        s_allowedCommands.insert(norm);
        QString comp = norm;
        comp.remove(QLatin1Char(' '));
        s_allowedCommands.insert(comp);
    }

    s_allowedHeaders.clear();
    for (const auto& hdr : allowedHeaders) {
        s_allowedHeaders.insert(hdr.trimmed().toUpper());
    }

    s_allowedCraFilters.clear();
    for (const auto& cra : allowedCraFilters) {
        s_allowedCraFilters.insert(cra.trimmed().toUpper());
    }

    s_hasActiveContext.store(true);
}

void SafetyGuard::clearAuthorizedContext()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_allowedCommands.clear();
    s_allowedHeaders.clear();
    s_allowedCraFilters.clear();
    s_hasActiveContext.store(false);
}

bool SafetyGuard::hasActiveVehicleContext()
{
    return s_hasActiveContext.load();
}

void SafetyGuard::setUserConfirmedClearDtc(bool confirmed)
{
    s_userConfirmedClearDtc.store(confirmed);
}

bool SafetyGuard::isUserConfirmedClearDtc()
{
    return s_userConfirmedClearDtc.load();
}

bool SafetyGuard::isCommandSafe(const QString& rawCommand, bool userConfirmedClearDtc, QString* blockReason)
{
    QString cmd = rawCommand.trimmed().toUpper();
    if (cmd.isEmpty()) {
        if (blockReason) *blockReason = QStringLiteral("Comando vacío");
        return false;
    }

    // Normalizar comando compacto
    QString compactCmd = cmd;
    compactCmd.remove(QLatin1Char(' '));

    // =========================================================================
    // 1. LISTA NEGRA ABSOLUTA (Peligro crítico de integridad de ECU y bus CAN)
    // =========================================================================
    if (compactCmd.startsWith(QStringLiteral("ATBI"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: AT BI (Bypass Initialization) desactiva protecciones de sincronización");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("ATCSM0"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: AT CSM0 desactiva el monitoreo silencioso CAN");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("ATPP"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Modificación de parámetros EEPROM del adaptador prohibida");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("1002"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Sesión de reprogramación ECU prohibida (UDS 0x10 0x02)");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("11"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Reinicio de ECU prohibido (UDS 0x11 ECUReset)");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("27"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Intento de acceso de seguridad ECU prohibido (UDS 0x27 SecurityAccess)");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("28"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Alteración de comunicación en red vehicular (UDS 0x28 CommunicationControl)");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("2E"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Escritura en memoria de ECU bloqueada (UDS 0x2E WriteDataByIdentifier)");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("2F"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Control y actuadores de ECU bloqueados (UDS 0x2F InputOutputControl)");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("31"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Ejecución de rutinas de ECU bloqueada (UDS 0x31 RoutineControl)");
        return false;
    }
    if (compactCmd.startsWith(QStringLiteral("34")) || compactCmd.startsWith(QStringLiteral("36")) || compactCmd.startsWith(QStringLiteral("37"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Operaciones de flasheo de firmware bloqueadas");
        return false;
    }

    // =========================================================================
    // 2. ENCLAVAMIENTO DE SEGURIDAD PARA BORRADO DE CÓDIGOS (Modo 04 / UDS 0x14)
    // =========================================================================
    bool isClearDtcCommand = (compactCmd == QStringLiteral("04") || compactCmd.startsWith(QStringLiteral("04")) ||
                              compactCmd.startsWith(QStringLiteral("14")));
    if (isClearDtcCommand) {
        if (!userConfirmedClearDtc) {
            if (blockReason) *blockReason = QStringLiteral("Borrado de DTC bloqueado por defecto. Requiere confirmación de usuario con motor apagado.");
            return false;
        }
        return true;
    }

    // =========================================================================
    // 3. COMANDOS DE CONFIGURACIÓN DEL ADAPTADOR (AT / ST)
    // =========================================================================
    // Comandos de cabecera de transmisión AT SH
    if (compactCmd.startsWith(QStringLiteral("ATSH"))) {
        QString hdr = compactCmd.mid(4).trimmed();
        if (hdr == QStringLiteral("7DF")) {
            return true; // Broadcast funcional siempre permitido para descubrimiento
        }
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_hasActiveContext.load() && !s_allowedHeaders.contains(hdr)) {
            if (blockReason) {
                *blockReason = QStringLiteral("Header CAN 'AT SH %1' no está registrado para el vehículo activo").arg(hdr);
            }
            return false;
        }
        return true;
    }

    // Comandos de filtro de recepción AT CRA
    if (compactCmd.startsWith(QStringLiteral("ATCRA"))) {
        QString cra = compactCmd.mid(5).trimmed();
        if (cra.isEmpty()) {
            return true; // Restablecer filtro abierto / por defecto
        }
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_hasActiveContext.load()) {
            bool authorized = s_allowedCraFilters.contains(cra);
            if (!authorized) {
                // Verificar si existe como ID en DBC o OBDb
                bool ok = false;
                uint val = cra.toUInt(&ok, 16);
                if (ok) {
                    QString noLeadZero = QString::number(val, 16).toUpper();
                    authorized = s_allowedCraFilters.contains(noLeadZero);
                }
            }
            if (!authorized) {
                if (blockReason) {
                    *blockReason = QStringLiteral("Filtro de recepción 'AT CRA %1' no registrado en OBDb/DBC").arg(cra);
                }
                return false;
            }
        }
        return true;
    }

    // Configuración AT básica segura
    static const QRegularExpression safeBaseAtRegex(
        QStringLiteral("^(AT\\s*(Z|E[01]|L[01]|S[01]|H[01]|SP\\s*[0-9A-F]|DP|DPN|CAF[01]|CFC[01]|CSM1|AT[012]|RV|I)|STPX.*)$"),
        QRegularExpression::CaseInsensitiveOption);

    if (safeBaseAtRegex.match(cmd).hasMatch()) {
        return true;
    }

    // =========================================================================
    // 4. VALIDACIÓN DINÁMICA DE COMANDOS DE DIAGNÓSTICO (01, 03, 07, 09, 19, 21, 22)
    // =========================================================================
    bool isDiagService = (compactCmd.startsWith(QStringLiteral("01")) ||
                          compactCmd.startsWith(QStringLiteral("02")) ||
                          compactCmd == QStringLiteral("03") ||
                          compactCmd == QStringLiteral("07") ||
                          compactCmd.startsWith(QStringLiteral("09")) ||
                          compactCmd.startsWith(QStringLiteral("19")) ||
                          compactCmd.startsWith(QStringLiteral("21")) ||
                          compactCmd.startsWith(QStringLiteral("22")) ||
                          compactCmd == QStringLiteral("3E"));

    if (isDiagService) {
        // Modo 01 00 (Broadcast funcional para detectar protocolo) y TesterPresent 3E siempre permitidos
        if (compactCmd == QStringLiteral("0100") || compactCmd.startsWith(QStringLiteral("3E"))) {
            return true;
        }

        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_hasActiveContext.load()) {
            if (blockReason) {
                *blockReason = QStringLiteral("No hay perfil vehicular cargado en SafetyGuard. Comando '%1' bloqueado preventivamente.").arg(cmd);
            }
            return false;
        }

        // Modos generales de DTCs y metadatos estándar SAE J1979 / UDS: Modo 03, Modo 07, Modo 09 02 y UDS 0x19 0x02
        if (compactCmd == QStringLiteral("03") || compactCmd == QStringLiteral("07") ||
            compactCmd == QStringLiteral("0902") || compactCmd == QStringLiteral("0900") ||
            compactCmd.startsWith(QStringLiteral("1902")) || compactCmd.startsWith(QStringLiteral("1901"))) {
            return true;
        }

        // Verificar si el comando específico está en la lista de OBDb/DBC autorizados
        if (s_allowedCommands.contains(cmd) || s_allowedCommands.contains(compactCmd)) {
            return true;
        }

        if (blockReason) {
            *blockReason = QStringLiteral("Comando de diagnóstico '%1' no está registrado en el perfil OBDb del vehículo").arg(cmd);
        }
        return false;
    }

    if (blockReason) {
        *blockReason = QStringLiteral("Comando '%1' no autorizado por el cortafuegos de seguridad").arg(cmd);
    }
    return false;
}
