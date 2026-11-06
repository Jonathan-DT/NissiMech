#pragma once

#include <QString>
#include <QSet>
#include <atomic>
#include <mutex>

/**
 * @brief Cortafuegos de seguridad de nivel automotriz para comandos OBD2 y UDS.
 * 
 * Implementa una lista blanca dinámica vinculada a los archivos OBDb y DBC cargados,
 * además de una lista negra absoluta de comandos peligrosos hacia la ECU.
 */
class SafetyGuard
{
public:
    SafetyGuard() = delete;

    /**
     * @brief Configura el contexto de comandos, cabeceras y filtros autorizados para el vehículo activo.
     */
    static void setAuthorizedContext(const QSet<QString>& allowedCommands,
                                     const QSet<QString>& allowedHeaders,
                                     const QSet<QString>& allowedCraFilters);

    /**
     * @brief Limpia el contexto activo. En este estado solo se permiten comandos AT básicos de configuración.
     */
    static void clearAuthorizedContext();

    /**
     * @brief Indica si existe un perfil vehicular cargado en el cortafuegos.
     */
    static bool hasActiveVehicleContext();

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
    static inline std::atomic<bool> s_hasActiveContext{false};
    static inline std::mutex s_mutex;
    static inline QSet<QString> s_allowedCommands;
    static inline QSet<QString> s_allowedHeaders;
    static inline QSet<QString> s_allowedCraFilters;
};
