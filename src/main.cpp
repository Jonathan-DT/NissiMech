#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
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

/**
 * @brief Controlador del panel de diagnóstico para enlazar el Core C++20 con QtQuick/QML.
 */
class DiagnosticController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(int selectedAdapterIndex READ selectedAdapterIndex WRITE setSelectedAdapterIndex NOTIFY selectedAdapterIndexChanged)
    Q_PROPERTY(QStringList availableAdapters READ availableAdapters CONSTANT)
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
        , m_pollTimer(new QTimer(this))
    {
        m_dtcManager->initializeDatabase();

        m_availableAdapters = {
            QStringLiteral("Simulador: Toyota Camry 2006 (CAN 11/500)"),
            QStringLiteral("Simulador: Chevrolet Beat 2018 (CAN 11/500)"),
            QStringLiteral("Hardware USB: Vgate vLinker FS / OBDLink EX"),
            QStringLiteral("Hardware Nativo: Linux SocketCAN (can0)")
        };

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
        if (m_selectedAdapterIndex == 0 || m_selectedAdapterIndex == 1) {
            ok = m_driver->openConnection(QStringLiteral("MOCK"));
        } else if (m_selectedAdapterIndex == 2) {
            ok = m_driver->openConnection(portName.isEmpty() ? QStringLiteral("/dev/ttyUSB0") : portName, 115200);
        } else if (m_selectedAdapterIndex == 3) {
            ok = m_driver->openConnection(portName.isEmpty() ? QStringLiteral("can0") : portName, 500000);
        }

        if (ok) {
            m_obdProtocol = std::make_unique<ObdProtocol>(m_driver.get(), m_dtcManager.get());
            m_udsProtocol = std::make_unique<UdsProtocol>(m_driver.get(), m_dtcManager.get());
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
        emit rpmChanged();
        emit speedChanged();
        emit connectionStateChanged();
    }

    void pollLiveData() {
        if (!isConnected() || !m_obdProtocol) return;

        m_rpm = m_obdProtocol->readRpm();
        m_speed = m_obdProtocol->readSpeed();
        m_coolantTemp = m_obdProtocol->readCoolantTemp();
        m_intakeAirTemp = m_obdProtocol->readIntakeAirTemp();
        m_maf = m_obdProtocol->readMaf();
        m_throttle = m_obdProtocol->readThrottle();
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

    void readAbsToyotaDtcs() {
        if (!isConnected() || !m_udsProtocol) return;
        QList<DtcInfo> dtcs = m_udsProtocol->readModuleDtcs(TargetModule::Brakes_ABS_Toyota);
        updateDtcList(dtcs);
    }

    void readAbsBeatDtcs() {
        if (!isConnected() || !m_udsProtocol) return;
        QList<DtcInfo> dtcs = m_udsProtocol->readModuleDtcs(TargetModule::Brakes_ABS_ISO);
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
    void setupDriver() {
        if (m_selectedAdapterIndex == 0) {
            auto mock = std::make_unique<MockElmDriver>();
            mock->setVehicleProfile(MockElmDriver::VehicleProfile::ToyotaCamry2006);
            m_driver = std::move(mock);
        } else if (m_selectedAdapterIndex == 1) {
            auto mock = std::make_unique<MockElmDriver>();
            mock->setVehicleProfile(MockElmDriver::VehicleProfile::ChevroletBeat2018);
            m_driver = std::move(mock);
        } else if (m_selectedAdapterIndex == 2) {
            m_driver = std::make_unique<SerialElmDriver>();
        } else if (m_selectedAdapterIndex == 3) {
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
            m_dtcList.append(item);
        }
        emit dtcListChanged();
    }

    std::unique_ptr<IObdInterface> m_driver;
    std::unique_ptr<DtcManager> m_dtcManager;
    std::unique_ptr<ObdProtocol> m_obdProtocol;
    std::unique_ptr<UdsProtocol> m_udsProtocol;
    QTimer* m_pollTimer{nullptr};

    int m_selectedAdapterIndex{0};
    QStringList m_availableAdapters;
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
    app.setApplicationVersion(QStringLiteral("1.0.0"));

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

    // Si no se carga desde qrc, intentar ruta local directa
    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        engine.load(QUrl::fromLocalFile(QStringLiteral("src/ui/MainDashboard.qml")));
    }

    return app.exec();
}

#include "main.moc"

