#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QDir>
#include <QFileInfo>
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
            ok = m_driver->openConnection(portName.isEmpty() ? QStringLiteral("/dev/ttyUSB0") : portName, 115200);
        } else if (m_selectedAdapterIndex == 2) {
            ok = m_driver->openConnection(portName.isEmpty() ? QStringLiteral("can0") : portName, 500000);
        }

        if (ok) {
            m_obdProtocol = std::make_unique<ObdProtocol>(m_driver.get(), m_dtcManager.get(), m_obdbLoader.get(), m_dbcParser.get());
            m_udsProtocol = std::make_unique<UdsProtocol>(m_driver.get(), m_dtcManager.get(), m_obdbLoader.get(), m_dbcParser.get());

            // Detectar protocolo físico real/simulado
            detectVehicleProtocol();

            m_pollTimer->start(200);
            pollVin();
            readStoredDtcs();
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

    void clearDtcs(bool userConfirmed) {
        if (!isConnected() || !m_obdProtocol) return;
        bool ok = m_obdProtocol->clearDtcs(userConfirmed);
        if (ok) {
            m_dtcList.clear();
            emit dtcListChanged();
        }
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

        // Rutas candidatas
        QString jsonPath = QStringLiteral("opendbc/%1/signalsets/v3/default.json").arg(vFolder);
        if (!QFileInfo::exists(jsonPath)) {
            jsonPath = QStringLiteral("../opendbc/%1/signalsets/v3/default.json").arg(vFolder);
        }

        // Cargar OBDb JSON
        m_obdbLoader->clear();
        if (QFileInfo::exists(jsonPath)) {
            m_obdbLoader->loadFile(jsonPath);
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

        for (const QString& id : m_availableSignals) {
            const ObdbSignal* s = m_obdbLoader->findSignal(id);
            if (!s) continue;

            if (m_rpmSignalId.isEmpty() && (s->suggestedMetric == QStringLiteral("engineSpeed") || id.contains(QStringLiteral("RPM")) || id.contains(QStringLiteral("ENGINE_SPEED")))) {
                m_rpmSignalId = id;
            } else if (m_speedSignalId.isEmpty() && (s->suggestedMetric == QStringLiteral("speed") || id.contains(QStringLiteral("SPEED")) || id.contains(QStringLiteral("VSS")))) {
                m_speedSignalId = id;
            } else if (m_coolantSignalId.isEmpty() && (s->suggestedMetric == QStringLiteral("coolantTemperature") || id.contains(QStringLiteral("COOLANT")))) {
                m_coolantSignalId = id;
            } else if (m_intakeSignalId.isEmpty() && (s->suggestedMetric == QStringLiteral("intakeAirTemperature") || id.contains(QStringLiteral("INTAKE")))) {
                m_intakeSignalId = id;
            } else if (m_mafSignalId.isEmpty() && (s->suggestedMetric == QStringLiteral("massAirFlow") || id.contains(QStringLiteral("MAF")))) {
                m_mafSignalId = id;
            } else if (m_throttleSignalId.isEmpty() && (s->suggestedMetric == QStringLiteral("throttlePosition") || id.contains(QStringLiteral("LOAD")) || id.contains(QStringLiteral("THROTTLE")))) {
                m_throttleSignalId = id;
            }
        }
    }

    void setupDriver() {
        if (m_selectedAdapterIndex == 0) {
            auto mock = std::make_unique<MockElmDriver>();
            m_driver = std::move(mock);
            loadVehicleProfile(m_selectedVehicleIndex);
        } else if (m_selectedAdapterIndex == 1) {
            m_driver = std::make_unique<SerialElmDriver>();
        } else if (m_selectedAdapterIndex == 2) {
            m_driver = std::make_unique<SocketCanDriver>();
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
};

int main(int argc, char* argv[])
{
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
