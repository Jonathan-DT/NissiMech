#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QTime>
#include <QSerialPortInfo>
#include <QDebug>
#include <memory>

#include "hal/MockElmDriver.h"
#include "hal/SerialElmDriver.h"
#include "hal/SocketCanDriver.h"
#include "core/SafetyGuard.h"
#include "core/PidDecoder.h"
#include "core/DtcManager.h"
#include "core/ObdProtocol.h"
#include "core/UdsProtocol.h"
#include "core/ObdbJsonLoader.h"
#include "core/DbcParser.h"

/**
 * @brief Controlador desacoplado del panel de diagnóstico para enlazar el Core C++20 con QtQuick/QML.
 * Gestiona el descubrimiento dinámico de perfiles de vehículos en opendbc/, carga de OBDb y DBC,
 * y enrutamiento seguro de comandos hacia el adaptador y el cortafuegos SafetyGuard.
 */
class DiagnosticController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(int selectedAdapterIndex READ selectedAdapterIndex WRITE setSelectedAdapterIndex NOTIFY selectedAdapterIndexChanged)
    Q_PROPERTY(QStringList availableAdapters READ availableAdapters CONSTANT)
    Q_PROPERTY(QStringList availableVehicles READ availableVehicles NOTIFY availableVehiclesChanged)
    Q_PROPERTY(int selectedVehicleIndex READ selectedVehicleIndex WRITE setSelectedVehicleIndex NOTIFY selectedVehicleIndexChanged)
    Q_PROPERTY(QString activeVehicleName READ activeVehicleName NOTIFY activeVehicleNameChanged)
    Q_PROPERTY(QString activeProtocol READ activeProtocol NOTIFY activeProtocolChanged)
    Q_PROPERTY(QVariantList availableModules READ availableModules NOTIFY availableModulesChanged)
    Q_PROPERTY(QStringList availableSignals READ availableSignals NOTIFY availableSignalsChanged)
    Q_PROPERTY(QVariantList availableSerialPorts READ availableSerialPorts NOTIFY availableSerialPortsChanged)
    Q_PROPERTY(QString selectedSerialPort READ selectedSerialPort WRITE setSelectedSerialPort NOTIFY selectedSerialPortChanged)
    Q_PROPERTY(QStringList communicationLog READ communicationLog NOTIFY communicationLogChanged)

    // Valores en vivo
    Q_PROPERTY(double rpm READ rpm NOTIFY rpmChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(double coolantTemp READ coolantTemp NOTIFY coolantTempChanged)
    Q_PROPERTY(double intakeAirTemp READ intakeAirTemp NOTIFY intakeAirTempChanged)
    Q_PROPERTY(double maf READ maf NOTIFY mafChanged)
    Q_PROPERTY(double throttle READ throttle NOTIFY throttleChanged)
    Q_PROPERTY(double batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)
    Q_PROPERTY(QString vin READ vin NOTIFY vinChanged)
    Q_PROPERTY(QVariantList dtcList READ dtcList NOTIFY dtcListChanged)

public:
    explicit DiagnosticController(QObject* parent = nullptr)
        : QObject(parent)
        , m_dtcManager(std::make_unique<DtcManager>())
        , m_obdbLoader(std::make_unique<ObdbJsonLoader>())
        , m_dbcParser(std::make_unique<DbcParser>())
        , m_pollTimer(new QTimer(this))
    {
        m_dtcManager->initializeDatabase();

        m_availableAdapters = {
            QStringLiteral("Simulador en Memoria (MockElmDriver)"),
            QStringLiteral("Hardware USB: Vgate vLinker FS / OBDLink EX"),
            QStringLiteral("Hardware Nativo: Linux SocketCAN (can0)")
        };

        refreshSerialPorts();
        discoverVehicles();
        if (!m_availableVehicles.isEmpty()) {
            loadVehicleProfile(0);
        }

        connect(m_pollTimer, &QTimer::timeout, this, &DiagnosticController::pollLiveData);
        setupDriver();
    }

    bool isConnected() const { return m_driver && m_driver->isConnected(); }
    int selectedAdapterIndex() const { return m_selectedAdapterIndex; }
    void setSelectedAdapterIndex(int idx) {
        if (m_selectedAdapterIndex != idx) {
            disconnectAdapter();
            m_selectedAdapterIndex = idx;
            setupDriver();
            emit selectedAdapterIndexChanged();
        }
    }

    QStringList availableAdapters() const { return m_availableAdapters; }
    QStringList availableVehicles() const { return m_availableVehicles; }
    int selectedVehicleIndex() const { return m_selectedVehicleIndex; }
    void setSelectedVehicleIndex(int idx) {
        if (m_selectedVehicleIndex != idx && idx >= 0 && idx < m_availableVehicles.size()) {
            m_selectedVehicleIndex = idx;
            loadVehicleProfile(idx);
            emit selectedVehicleIndexChanged();
        }
    }

    QString activeVehicleName() const { return m_activeVehicleName; }
    QString activeProtocol() const { return m_activeProtocol; }
    QVariantList availableModules() const { return m_availableModules; }
    QStringList availableSignals() const { return m_availableSignals; }
    QVariantList availableSerialPorts() const { return m_availableSerialPorts; }
    QString selectedSerialPort() const { return m_selectedSerialPort; }
    void setSelectedSerialPort(const QString& port) {
        if (m_selectedSerialPort != port) {
            m_selectedSerialPort = port;
            emit selectedSerialPortChanged();
        }
    }
    QStringList communicationLog() const { return m_communicationLog; }

    double rpm() const { return m_rpm; }
    double speed() const { return m_speed; }
    double coolantTemp() const { return m_coolantTemp; }
    double intakeAirTemp() const { return m_intakeAirTemp; }
    double maf() const { return m_maf; }
    double throttle() const { return m_throttle; }
    double batteryVoltage() const { return m_batteryVoltage; }
    QString vin() const { return m_vin; }
    QVariantList dtcList() const { return m_dtcList; }

public slots:
    void connectAdapter(const QString& portName = QString()) {
        if (!m_driver) setupDriver();
        if (!m_driver) return;

        bool ok = false;
        if (m_selectedAdapterIndex == 0) {
            ok = m_driver->openConnection(QStringLiteral("MOCK"));
        } else if (m_selectedAdapterIndex == 1) {
            QString port = portName;
            if (port.isEmpty()) {
                port = !m_selectedSerialPort.isEmpty() ? m_selectedSerialPort : QStringLiteral("/dev/ttyUSB0");
            }
            ok = m_driver->openConnection(port, 115200);
        } else if (m_selectedAdapterIndex == 2) {
            ok = m_driver->openConnection(portName.isEmpty() ? QStringLiteral("can0") : portName, 500000);
        }

        if (ok) {
            m_obdProtocol = std::make_unique<ObdProtocol>(m_driver.get(), m_dtcManager.get(), m_obdbLoader.get(), m_dbcParser.get());
            m_udsProtocol = std::make_unique<UdsProtocol>(m_driver.get(), m_dtcManager.get(), m_obdbLoader.get(), m_dbcParser.get());

            addLogEntry(QStringLiteral("INFO"), QStringLiteral("Conexión establecida con %1").arg(m_availableAdapters.value(m_selectedAdapterIndex)));

            // Detectar protocolo físico real/simulado
            detectVehicleProtocol();

            m_pollTimer->start(200);
            pollVin();
            readStoredDtcs();
        } else {
            addLogEntry(QStringLiteral("ERROR"), QStringLiteral("No se pudo conectar al adaptador (%1).").arg(m_availableAdapters.value(m_selectedAdapterIndex)));
        }
        emit connectionStateChanged();
    }

    void disconnectAdapter() {
        m_pollTimer->stop();
        if (m_driver) {
            m_driver->closeConnection();
        }
        m_rpm = 0.0;
        m_speed = 0.0;
        m_activeProtocol = QStringLiteral("Desconectado");
        addLogEntry(QStringLiteral("INFO"), QStringLiteral("Adaptador desconectado por el usuario."));
        emit activeProtocolChanged();
        emit rpmChanged();
        emit speedChanged();
        emit connectionStateChanged();
    }

    void detectVehicleProtocol() {
        if (!isConnected() || !m_obdProtocol) return;
        m_activeProtocol = m_obdProtocol->detectActiveProtocol();
        emit activeProtocolChanged();
    }

    void pollLiveData() {
        if (!isConnected() || !m_obdProtocol) return;

        // Búsqueda dinámica de señales en el catálogo cargado
        if (!m_rpmSignalId.isEmpty()) {
            auto val = m_obdProtocol->querySignal(m_rpmSignalId);
            if (val.has_value()) m_rpm = *val;
        }
        if (!m_speedSignalId.isEmpty()) {
            auto val = m_obdProtocol->querySignal(m_speedSignalId);
            if (val.has_value()) m_speed = *val;
        }
        if (!m_coolantSignalId.isEmpty()) {
            auto val = m_obdProtocol->querySignal(m_coolantSignalId);
            if (val.has_value()) m_coolantTemp = *val;
        }
        if (!m_intakeSignalId.isEmpty()) {
            auto val = m_obdProtocol->querySignal(m_intakeSignalId);
            if (val.has_value()) m_intakeAirTemp = *val;
        }
        if (!m_mafSignalId.isEmpty()) {
            auto val = m_obdProtocol->querySignal(m_mafSignalId);
            if (val.has_value()) m_maf = *val;
        }
        if (!m_throttleSignalId.isEmpty()) {
            auto val = m_obdProtocol->querySignal(m_throttleSignalId);
            if (val.has_value()) m_throttle = *val;
        }

        m_batteryVoltage = m_obdProtocol->readBatteryVoltage();

        emit rpmChanged();
        emit speedChanged();
        emit coolantTempChanged();
        emit intakeAirTempChanged();
        emit mafChanged();
        emit throttleChanged();
        emit batteryVoltageChanged();
    }

    void pollVin() {
        if (!isConnected() || !m_obdProtocol) return;
        m_vin = m_obdProtocol->readVin();
        emit vinChanged();
    }

    void readStoredDtcs() {
        if (!isConnected() || !m_obdProtocol) return;
        QList<DtcInfo> dtcs = m_obdProtocol->readStoredDtcs();
        updateDtcList(dtcs);
    }

    void readPendingDtcs() {
        if (!isConnected() || !m_obdProtocol) return;
        QList<DtcInfo> dtcs = m_obdProtocol->readPendingDtcs();
        updateDtcList(dtcs);
    }

    void scanModuleDtcs(const QString& txHeader, const QString& rxCra = QString()) {
        if (!isConnected() || !m_udsProtocol || txHeader.isEmpty()) return;
        QList<DtcInfo> dtcs = m_udsProtocol->readModuleDtcs(txHeader, rxCra);
        updateDtcList(dtcs);
    }

    Q_INVOKABLE void refreshSerialPorts() {
        m_availableSerialPorts.clear();
        QString recommendedPort;
        const auto ports = QSerialPortInfo::availablePorts();
        const QStringList keywords = {
            QStringLiteral("vLinker"),
            QStringLiteral("OBDLink"),
            QStringLiteral("STN"),
            QStringLiteral("FTDI"),
            QStringLiteral("CH340"),
            QStringLiteral("CP210")
        };

        for (const QSerialPortInfo& info : ports) {
            QVariantMap portMap;
            portMap[QStringLiteral("portName")] = info.portName();
            portMap[QStringLiteral("systemLocation")] = info.systemLocation();
            portMap[QStringLiteral("description")] = info.description();
            portMap[QStringLiteral("manufacturer")] = info.manufacturer();

            bool isRec = false;
            for (const QString& kw : keywords) {
                if (info.description().contains(kw, Qt::CaseInsensitive) ||
                    info.manufacturer().contains(kw, Qt::CaseInsensitive)) {
                    isRec = true;
                    break;
                }
            }
            portMap[QStringLiteral("isRecommended")] = isRec;
            m_availableSerialPorts.append(portMap);

            if (isRec && recommendedPort.isEmpty()) {
                recommendedPort = info.systemLocation().isEmpty() ? info.portName() : info.systemLocation();
            }
        }

        if (!recommendedPort.isEmpty()) {
            setSelectedSerialPort(recommendedPort);
        } else if (!m_availableSerialPorts.isEmpty() && m_selectedSerialPort.isEmpty()) {
            const auto firstMap = m_availableSerialPorts.first().toMap();
            QString loc = firstMap.value(QStringLiteral("systemLocation")).toString();
            if (loc.isEmpty()) loc = firstMap.value(QStringLiteral("portName")).toString();
            setSelectedSerialPort(loc);
        }

        emit availableSerialPortsChanged();
    }

    Q_INVOKABLE void sendDiagnosticCommand(const QString& rawCmd) {
        if (!m_driver || !m_driver->isConnected()) {
            addLogEntry(QStringLiteral("ERROR"), QStringLiteral("Adaptador desconectado. Conecte un adaptador para transmitir."));
            return;
        }

        QString trimmedCmd = rawCmd.trimmed();
        if (trimmedCmd.isEmpty()) return;

        QString blockReason;
        if (!SafetyGuard::isCommandSafe(trimmedCmd, false, &blockReason)) {
            QString timeStr = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
            QString msg = QStringLiteral("[%1] [BLOQUEADO] Comando rechazado por SafetyGuard: %2 (%3)")
                              .arg(timeStr, trimmedCmd, blockReason);
            m_communicationLog.append(msg);
            while (m_communicationLog.size() > 300) {
                m_communicationLog.removeFirst();
            }
            emit communicationLogChanged();
            return;
        }

        addLogEntry(QStringLiteral("TX"), trimmedCmd);
        m_driver->sendRawCommand(trimmedCmd);
    }

    Q_INVOKABLE void clearCommunicationLog() {
        m_communicationLog.clear();
        emit communicationLogChanged();
    }

    Q_INVOKABLE bool exportCommunicationLog(const QString& customPath = QString()) {
        QString filePath = customPath;
        if (filePath.isEmpty()) {
            QString timeStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
            filePath = QStringLiteral("nissimech_comm_log_%1.txt").arg(timeStamp);
        }

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit statusAlert(QStringLiteral("Exportar Log"),
                             QStringLiteral("No se pudo crear el archivo de log en: %1").arg(filePath),
                             true);
            return false;
        }

        QTextStream out(&file);
        out << "=========================================================\n";
        out << "NissiMech OBD2/CAN Communication Log Export\n";
        out << "Fecha: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "Vehículo: " << m_activeVehicleName << "\n";
        out << "Protocolo: " << m_activeProtocol << "\n";
        out << "Total entradas: " << m_communicationLog.size() << "\n";
        out << "=========================================================\n\n";

        for (const QString& line : m_communicationLog) {
            out << line << "\n";
        }
        file.close();

        emit statusAlert(QStringLiteral("Exportar Log"),
                         QStringLiteral("Log exportado exitosamente a:\n%1").arg(QFileInfo(filePath).absoluteFilePath()),
                         false);
        return true;
    }

    Q_INVOKABLE void clearDtcs(bool userConfirmed) {
        if (!isConnected() || !m_obdProtocol) {
            emit statusAlert(QStringLiteral("Error de Conexión"),
                             QStringLiteral("El escáner no está conectado al vehículo."),
                             true);
            return;
        }

        QString statusMsg;
        addLogEntry(QStringLiteral("TX"), QStringLiteral("04 (Solicitud de borrado DTC Modo 04)"));
        bool ok = m_obdProtocol->clearDtcs(userConfirmed, &statusMsg);
        addLogEntry(ok ? QStringLiteral("INFO") : QStringLiteral("BLOQUEADO"), statusMsg);

        emit statusAlert(QStringLiteral("Borrado de Códigos DTC"), statusMsg, !ok);

        if (ok) {
            readStoredDtcs();
        }
    }

    void addLogEntry(const QString& direction, const QString& message) {
        QString timeStr = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
        QString entry;
        if (direction == QStringLiteral("TX")) {
            entry = QStringLiteral("[%1] TX -> %2").arg(timeStr, message);
        } else if (direction == QStringLiteral("RX")) {
            entry = QStringLiteral("[%1] RX <- %2").arg(timeStr, message);
        } else if (direction == QStringLiteral("BLOQUEADO")) {
            entry = QStringLiteral("[%1] [BLOQUEADO] %2").arg(timeStr, message);
        } else {
            entry = QStringLiteral("[%1] [%2] %3").arg(timeStr, direction, message);
        }

        m_communicationLog.append(entry);
        while (m_communicationLog.size() > 300) {
            m_communicationLog.removeFirst();
        }
        emit communicationLogChanged();
    }

signals:
    void connectionStateChanged();
    void selectedAdapterIndexChanged();
    void availableVehiclesChanged();
    void selectedVehicleIndexChanged();
    void activeVehicleNameChanged();
    void activeProtocolChanged();
    void availableModulesChanged();
    void availableSignalsChanged();
    void availableSerialPortsChanged();
    void selectedSerialPortChanged();
    void communicationLogChanged();
    void statusAlert(const QString& title, const QString& message, bool isError);
    void rpmChanged();
    void speedChanged();
    void coolantTempChanged();
    void intakeAirTempChanged();
    void mafChanged();
    void throttleChanged();
    void batteryVoltageChanged();
    void vinChanged();
    void dtcListChanged();

private:
    void discoverVehicles() {
        m_availableVehicles.clear();
        const QStringList searchDirs = {
            QStringLiteral("opendbc"),
            QStringLiteral("../opendbc"),
            QDir::current().filePath(QStringLiteral("opendbc"))
        };

        for (const QString& dirPath : searchDirs) {
            QDir baseDir(dirPath);
            if (baseDir.exists()) {
                QStringList entries = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString& vName : entries) {
                    if (!m_availableVehicles.contains(vName)) {
                        m_availableVehicles.append(vName);
                    }
                }
                if (!m_availableVehicles.isEmpty()) break;
            }
        }

        if (m_availableVehicles.isEmpty()) {
            m_availableVehicles.append(QStringLiteral("SAE-J1979"));
        }
        emit availableVehiclesChanged();
    }

    void loadVehicleProfile(int index) {
        if (index < 0 || index >= m_availableVehicles.size()) return;
        QString vFolder = m_availableVehicles.at(index);
        m_activeVehicleName = vFolder;

        // Determinar año modelo para filtrado en OBDb
        std::optional<int> modelYear;
        if (vFolder.contains(QStringLiteral("Camry"), Qt::CaseInsensitive)) {
            modelYear = 2006;
        } else if (vFolder.contains(QStringLiteral("Beat"), Qt::CaseInsensitive) || vFolder.contains(QStringLiteral("Spark"), Qt::CaseInsensitive)) {
            modelYear = 2018;
        }

        // 1. Cargar SIEMPRE la base estándar SAE-J1979 primero
        m_obdbLoader->clear();
        QString saeJsonPath = QStringLiteral("opendbc/SAE-J1979/signalsets/v3/default.json");
        if (!QFileInfo::exists(saeJsonPath)) {
            saeJsonPath = QStringLiteral("../opendbc/SAE-J1979/signalsets/v3/default.json");
        }
        m_obdbLoader->appendFile(saeJsonPath);

        // 2. Si no es el perfil genérico, fusionar las señales específicas del vehículo
        if (vFolder != QStringLiteral("SAE-J1979")) {
            QString jsonPath = QStringLiteral("opendbc/%1/signalsets/v3/default.json").arg(vFolder);
            if (!QFileInfo::exists(jsonPath)) {
                jsonPath = QStringLiteral("../opendbc/%1/signalsets/v3/default.json").arg(vFolder);
            }
            if (QFileInfo::exists(jsonPath)) {
                m_obdbLoader->appendFile(jsonPath, modelYear);
            }
        }

        // Cargar DBC de opendbc si está presente en la carpeta del vehículo
        m_dbcParser->clear();
        QString opendbcDir = QStringLiteral("opendbc/%1").arg(vFolder);
        if (!QDir(opendbcDir).exists()) {
            opendbcDir = QStringLiteral("../opendbc/%1").arg(vFolder);
        }
        QDir vDir(opendbcDir);
        QStringList dbcFiles = vDir.entryList(QStringList() << QStringLiteral("*.dbc"), QDir::Files);
        if (!dbcFiles.isEmpty()) {
            m_dbcParser->loadFile(vDir.filePath(dbcFiles.first()));
        }

        // Actualizar contexto de seguridad en SafetyGuard
        QSet<QString> authCmds = m_obdbLoader->authorizedCommands();
        QSet<QString> authHdrs = m_obdbLoader->authorizedHeaders();
        QSet<QString> authCra = m_obdbLoader->authorizedCraFilters();
        authCra.unite(m_dbcParser->authorizedHexIds());

        SafetyGuard::setAuthorizedContext(authCmds, authHdrs, authCra);

        // Actualizar módulos disponibles para la UI
        m_availableModules.clear();
        for (const auto& ecu : m_obdbLoader->ecus()) {
            QVariantMap m;
            m[QStringLiteral("header")] = ecu.hdr;
            m[QStringLiteral("type")] = ecu.type;
            m_availableModules.append(m);
        }

        // Lista de señales disponibles
        m_availableSignals = m_obdbLoader->availableSignalIds();

        // Mapear señales prioritarias dinámicamente
        resolveKeySignals();

        // Si el mock driver está activo, cargar el escenario correspondiente
        if (auto* mock = dynamic_cast<MockElmDriver*>(m_driver.get())) {
            QString scenarioFile;
            if (vFolder.contains(QStringLiteral("Camry"), Qt::CaseInsensitive)) {
                scenarioFile = QStringLiteral("toyota_camry_2006_test.json");
            } else if (vFolder.contains(QStringLiteral("Beat"), Qt::CaseInsensitive) || vFolder.contains(QStringLiteral("Spark"), Qt::CaseInsensitive)) {
                scenarioFile = QStringLiteral("chevrolet_beat_2018_test.json");
            } else {
                scenarioFile = QStringLiteral("toyota_camry_2006_test.json");
            }
            mock->loadScenario(scenarioFile);
        }

        emit activeVehicleNameChanged();
        emit availableModulesChanged();
        emit availableSignalsChanged();
    }

    void resolveKeySignals() {
        m_rpmSignalId.clear();
        m_speedSignalId.clear();
        m_coolantSignalId.clear();
        m_intakeSignalId.clear();
        m_mafSignalId.clear();
        m_throttleSignalId.clear();

        // 1. Priorizar SIEMPRE señales base legisladas SAE J1979 si existen en m_obdbLoader
        if (m_obdbLoader->findSignal(QStringLiteral("SAE_ENGINE_RPM")))
            m_rpmSignalId = QStringLiteral("SAE_ENGINE_RPM");
        if (m_obdbLoader->findSignal(QStringLiteral("SAE_VEHICLE_SPEED")))
            m_speedSignalId = QStringLiteral("SAE_VEHICLE_SPEED");
        if (m_obdbLoader->findSignal(QStringLiteral("SAE_COOLANT_TEMP")))
            m_coolantSignalId = QStringLiteral("SAE_COOLANT_TEMP");
        if (m_obdbLoader->findSignal(QStringLiteral("SAE_INTAKE_AIR_TEMP")))
            m_intakeSignalId = QStringLiteral("SAE_INTAKE_AIR_TEMP");
        if (m_obdbLoader->findSignal(QStringLiteral("SAE_MAF_AIR_FLOW")))
            m_mafSignalId = QStringLiteral("SAE_MAF_AIR_FLOW");
        if (m_obdbLoader->findSignal(QStringLiteral("SAE_THROTTLE_POSITION")))
            m_throttleSignalId = QStringLiteral("SAE_THROTTLE_POSITION");

        // 2. Si alguna no está asignada, buscar únicamente por suggestedMetric exacto
        for (const QString& id : m_availableSignals) {
            const ObdbSignal* s = m_obdbLoader->findSignal(id);
            if (!s) continue;

            if (m_rpmSignalId.isEmpty() && s->suggestedMetric == QStringLiteral("engineSpeed")) {
                m_rpmSignalId = id;
            }
            if (m_speedSignalId.isEmpty() && s->suggestedMetric == QStringLiteral("speed")) {
                m_speedSignalId = id;
            }
            if (m_coolantSignalId.isEmpty() && s->suggestedMetric == QStringLiteral("coolantTemperature")) {
                m_coolantSignalId = id;
            }
            if (m_intakeSignalId.isEmpty() && s->suggestedMetric == QStringLiteral("intakeAirTemperature")) {
                m_intakeSignalId = id;
            }
            if (m_mafSignalId.isEmpty() && s->suggestedMetric == QStringLiteral("massAirFlow")) {
                m_mafSignalId = id;
            }
            if (m_throttleSignalId.isEmpty() && s->suggestedMetric == QStringLiteral("throttlePosition")) {
                m_throttleSignalId = id;
            }
        }
    }

    void setupDriver() {
        if (m_driver) {
            m_driver->disconnect(this);
        }
        if (m_selectedAdapterIndex == 0) {
            auto mock = std::make_unique<MockElmDriver>();
            m_driver = std::move(mock);
            loadVehicleProfile(m_selectedVehicleIndex);
        } else if (m_selectedAdapterIndex == 1) {
            m_driver = std::make_unique<SerialElmDriver>();
        } else if (m_selectedAdapterIndex == 2) {
            m_driver = std::make_unique<SocketCanDriver>();
        }

        if (m_driver) {
            connect(m_driver.get(), &IObdInterface::rawDataReceived, this, [this](const QString& data) {
                QString clean = data.trimmed();
                clean.replace(QLatin1Char('\r'), QLatin1Char(' '));
                clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
                while (clean.contains(QStringLiteral("  "))) {
                    clean.replace(QStringLiteral("  "), QStringLiteral(" "));
                }
                if (!clean.isEmpty()) {
                    addLogEntry(QStringLiteral("RX"), clean);
                }
            });

            connect(m_driver.get(), &IObdInterface::errorOccurred, this, [this](const QString& err) {
                addLogEntry(QStringLiteral("ERROR"), err);
            });
        }
    }

    void updateDtcList(const QList<DtcInfo>& dtcs) {
        m_dtcList.clear();
        for (const auto& d : dtcs) {
            QVariantMap item;
            item[QStringLiteral("code")] = d.code;
            item[QStringLiteral("system")] = d.system;
            item[QStringLiteral("descriptionEs")] = d.descriptionEs;
            item[QStringLiteral("severity")] = d.severity;
            item[QStringLiteral("faultType")] = d.faultType;
            m_dtcList.append(item);
        }
        emit dtcListChanged();
    }

    std::unique_ptr<IObdInterface> m_driver;
    std::unique_ptr<DtcManager> m_dtcManager;
    std::unique_ptr<ObdbJsonLoader> m_obdbLoader;
    std::unique_ptr<DbcParser> m_dbcParser;
    std::unique_ptr<ObdProtocol> m_obdProtocol;
    std::unique_ptr<UdsProtocol> m_udsProtocol;
    QTimer* m_pollTimer{nullptr};

    int m_selectedAdapterIndex{0};
    int m_selectedVehicleIndex{0};
    QStringList m_availableAdapters;
    QStringList m_availableVehicles;
    QString m_activeVehicleName;
    QString m_activeProtocol{QStringLiteral("Desconectado")};
    QVariantList m_availableModules;
    QStringList m_availableSignals;

    // IDs de señales mapeadas activas
    QString m_rpmSignalId;
    QString m_speedSignalId;
    QString m_coolantSignalId;
    QString m_intakeSignalId;
    QString m_mafSignalId;
    QString m_throttleSignalId;

    double m_rpm{0.0};
    double m_speed{0.0};
    double m_coolantTemp{0.0};
    double m_intakeAirTemp{0.0};
    double m_maf{0.0};
    double m_throttle{0.0};
    double m_batteryVoltage{12.6};
    QString m_vin;
    QVariantList m_dtcList;

    QVariantList m_availableSerialPorts;
    QString m_selectedSerialPort;
    QStringList m_communicationLog;
};

int main(int argc, char* argv[])
{
#if defined(Q_OS_LINUX)
    // En entornos Linux/WSL2 sin permisos de lectura/escritura en los nodos /dev/dri (ej. usuario no en grupo render/video),
    // configurar software rasterizer antes de inicializar la pila gráfica para evitar advertencias de libEGL y caídas de driver Zink/DRI2.
    if (!qEnvironmentVariableIsSet("LIBGL_ALWAYS_SOFTWARE")) {
        bool hasDriAccess = false;
        const QStringList driCandidates = {
            QStringLiteral("/dev/dri/renderD128"),
            QStringLiteral("/dev/dri/card0")
        };
        for (const QString& node : driCandidates) {
            QFileInfo fi(node);
            if (fi.exists() && fi.isReadable() && fi.isWritable()) {
                hasDriAccess = true;
                break;
            }
        }
        if (!hasDriAccess) {
            qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
        }
    }
#endif

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OBD2_Scanner_Pro"));
    app.setOrganizationName(QStringLiteral("NissiMech"));
    app.setApplicationVersion(QStringLiteral("2.0.0"));

    QQmlApplicationEngine engine;

    DiagnosticController controller;
    engine.rootContext()->setContextProperty(QStringLiteral("diagnosticController"), &controller);

    const QUrl url(QStringLiteral("qrc:/ui/MainDashboard.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        engine.load(QUrl::fromLocalFile(QStringLiteral("src/ui/MainDashboard.qml")));
    }

    return app.exec();
}

#include "main.moc"
