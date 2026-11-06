#pragma once

#include <QString>
#include <atomic>

/**
 * @brief Cortafuegos de seguridad de nivel automotriz para comandos OBD2 y UDS.
 * 
 * Implementa una lista blanca estricta y lista negra absoluta de comandos hacia la ECU,
 * impidiendo modificaciones accidentales en la memoria flash/EEPROM, bloqueos de bus o
 * borrado no supervisado de códigos DTC.
 */
class SafetyGuard
{
public:
    SafetyGuard() = delete;

    /**
     * @brief Evalúa si un comando crudo es seguro para ser transmitido al vehículo.
     * @param rawCommand Comando ASCII o hexadecimal a enviar (ej. "01 0C", "AT Z", "04").
     * @param userConfirmedClearDtc Si el usuario confirmó expresamente el borrado de DTC y el motor está apagado.
     * @param blockReason Si no es nullptr, recibe la razón detallada del bloqueo en caso de rechazo.
     * @return true si el comando está explícitamente autorizado; false si fue bloqueado.
     */
    static bool isCommandSafe(const QString& rawCommand, bool userConfirmedClearDtc = false, QString* blockReason = nullptr);

    /**
     * @brief Establece el estado de autorización temporal para comandos de borrado de códigos.
     */
    static void setUserConfirmedClearDtc(bool confirmed);

    /**
     * @brief Consulta si la autorización temporal de borrado de códigos está activa.
     */
    static bool isUserConfirmedClearDtc();

private:
    static inline std::atomic<bool> s_userConfirmedClearDtc{false};
};

