#include "SafetyGuard.h"
#include <QRegularExpression>
#include <QStringList>

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

    // Normalizar espacios múltiples para análisis
    QString compactCmd = cmd;
    compactCmd.remove(QLatin1Char(' '));

    // =========================================================================
    // 1. LISTA NEGRA ABSOLUTA (Peligro crítico de integridad de ECU y bus)
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

    // UDS 10 02 (Programming Session)
    if (compactCmd.startsWith(QStringLiteral("1002"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Sesión de reprogramación ECU prohibida (UDS 0x10 0x02)");
        return false;
    }
    // UDS 11 (ECUReset)
    if (compactCmd.startsWith(QStringLiteral("11"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Reinicio de ECU prohibido (UDS 0x11 ECUReset)");
        return false;
    }
    // UDS 27 (SecurityAccess)
    if (compactCmd.startsWith(QStringLiteral("27"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Intento de acceso de seguridad ECU prohibido (UDS 0x27 SecurityAccess)");
        return false;
    }
    // UDS 28 (CommunicationControl)
    if (compactCmd.startsWith(QStringLiteral("28"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Alteración de comunicación en red vehicular (UDS 0x28 CommunicationControl)");
        return false;
    }
    // UDS 2E (WriteDataByIdentifier)
    if (compactCmd.startsWith(QStringLiteral("2E"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Escritura en memoria de ECU bloqueada (UDS 0x2E WriteDataByIdentifier)");
        return false;
    }
    // UDS 2F (InputOutputControlByIdentifier)
    if (compactCmd.startsWith(QStringLiteral("2F"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Control y actuadores de ECU bloqueados (UDS 0x2F InputOutputControl)");
        return false;
    }
    // UDS 31 (RoutineControl)
    if (compactCmd.startsWith(QStringLiteral("31"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Ejecución de rutinas de ECU bloqueada (UDS 0x31 RoutineControl)");
        return false;
    }
    // UDS 34 (RequestDownload) & 36 (TransferData)
    if (compactCmd.startsWith(QStringLiteral("34")) || compactCmd.startsWith(QStringLiteral("36")) || compactCmd.startsWith(QStringLiteral("37"))) {
        if (blockReason) *blockReason = QStringLiteral("Peligro: Operaciones de flasheo de firmware bloqueadas");
        return false;
    }

    // =========================================================================
    // 2. REGLA ESTRICTA DE BORRADO DE CÓDIGOS (Modo 04 y UDS 0x14)
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
    // 3. LISTA BLANCA DE COMANDOS SEGUROS
    // =========================================================================

    // Configuración AT / ST permitida
    static const QRegularExpression safeAtRegex(
        QStringLiteral("^(AT\\s*(Z|E[01]|L[01]|S[01]|H[01]|SP\\s*[0-9A-F]|DP|DPN|SH\\s*[0-9A-F]{3,8}|CRA(\\s*[0-9A-F]{3,8})?|CAF[01]|CFC[01]|CSM1|AT[012]|RV|I)|STPX.*)$"),
        QRegularExpression::CaseInsensitiveOption);

    if (safeAtRegex.match(cmd).hasMatch()) {
        return true;
    }

    // Modos OBD2 de solo lectura: 01, 02, 03, 07, 09, 0A
    static const QRegularExpression safeObd2Regex(
        QStringLiteral("^(01|02|03|07|09|0A)(\\s+[0-9A-F]{2})*$"),
        QRegularExpression::CaseInsensitiveOption);

    if (safeObd2Regex.match(cmd).hasMatch() ||
        compactCmd.startsWith(QStringLiteral("01")) ||
        compactCmd.startsWith(QStringLiteral("02")) ||
        compactCmd == QStringLiteral("03") ||
        compactCmd == QStringLiteral("07") ||
        compactCmd.startsWith(QStringLiteral("09")) ||
        compactCmd == QStringLiteral("0A")) {
        return true;
    }

    // Servicios de diagnóstico extendido de solo lectura:
    // 19 (UDS ReadDTCInformation)
    // 21 (ReadDataByLocalIdentifier - Toyota / GM)
    // 22 (UDS ReadDataByIdentifier)
    // 3E (TesterPresent)
    if (compactCmd.startsWith(QStringLiteral("19")) ||
        compactCmd.startsWith(QStringLiteral("21")) ||
        compactCmd.startsWith(QStringLiteral("22")) ||
        compactCmd.startsWith(QStringLiteral("3E"))) {
        return true;
    }

    if (blockReason) {
        *blockReason = QStringLiteral("Comando '%1' no figura en la lista blanca de seguridad vehicular").arg(cmd);
    }
    return false;
}

