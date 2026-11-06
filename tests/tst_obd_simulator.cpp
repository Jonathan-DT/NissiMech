#include <QtTest>
#include "../src/hal/MockElmDriver.h"
#include "../src/core/SafetyGuard.h"
#include "../src/core/PidDecoder.h"
#include "../src/core/DtcManager.h"
#include "../src/core/ObdProtocol.h"
#include "../src/core/UdsProtocol.h"

class TestObdSimulator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSafetyGuard();
    void testDtcBitwiseDecoding();
    void testToyotaCamry2006Simulation();
    void testChevroletBeat2018Simulation();
};

void TestObdSimulator::initTestCase()
{
    // Asegurar directorio y base de datos de prueba
    DtcManager mgr;
    QVERIFY(mgr.initializeDatabase());
}

void TestObdSimulator::testSafetyGuard()
{
    // Comandos de lectura seguros aprobados
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("01 0C")), "El PID de RPM (01 0C) debe ser aprobado");
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("03")), "La lectura de DTCs almacenados (Modo 03) debe ser aprobada");
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("19 02 FF")), "La lectura de DTCs UDS (19 02 FF) debe ser aprobada");

    // Borrado de DTC (Modo 04): rechazado por defecto sin confirmación
    QString blockReason;
    bool safeUnconfirmed = SafetyGuard::isCommandSafe(QStringLiteral("04"), false, &blockReason);
    QVERIFY2(!safeUnconfirmed, "El comando 04 debe ser bloqueado sin confirmación");
    QVERIFY2(!blockReason.isEmpty(), "Debe emitirse un motivo de bloqueo para el comando 04 no confirmado");

    // Borrado de DTC (Modo 04): aprobado con confirmación explícita
    bool safeConfirmed = SafetyGuard::isCommandSafe(QStringLiteral("04"), true);
    QVERIFY2(safeConfirmed, "El comando 04 debe ser aprobado con confirmación expresa");

    // Lista negra permanente: comandos peligrosos bloqueados siempre
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("AT BI")), "AT BI debe ser bloqueado siempre");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("27 01")), "UDS SecurityAccess (27 01) debe ser bloqueado siempre");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("2E F1 90")), "UDS WriteDataByIdentifier (2E F1 90) debe ser bloqueado siempre");
}

void TestObdSimulator::testDtcBitwiseDecoding()
{
    // Verificación de decodificación SAE J2012 Powertrain (00 -> 'P')
    QString p0171 = DtcManager::decodeDtcPair(0x01, 0x71);
    QCOMPARE(p0171, QStringLiteral("P0171"));

    QString p0740 = DtcManager::decodeDtcPair(0x07, 0x40);
    QCOMPARE(p0740, QStringLiteral("P0740"));

    // Verificación de decodificación Chassis (01 -> 'C') en codificación directa y estándar SAE J2012
    QString c0035_direct = DtcManager::decodeDtcPair(0xC0, 0x35);
    QCOMPARE(c0035_direct, QStringLiteral("C0035"));

    QString c0035_sae = DtcManager::decodeDtcPair(0x40, 0x35);
    QCOMPARE(c0035_sae, QStringLiteral("C0035"));

    QString c0200_direct = DtcManager::decodeDtcPair(0xC0, 0x20);
    QCOMPARE(c0200_direct, QStringLiteral("C0200"));

    QString c0200_sae = DtcManager::decodeDtcPair(0x42, 0x00);
    QCOMPARE(c0200_sae, QStringLiteral("C0200"));

    // Verificación con base de datos SQLite en Español
    DtcManager dtcMgr;
    QVERIFY(dtcMgr.initializeDatabase());

    DtcInfo infoP0171 = dtcMgr.getDtcDetails(QStringLiteral("P0171"));
    QCOMPARE(infoP0171.code, QStringLiteral("P0171"));
    QVERIFY2(infoP0171.descriptionEs.contains(QStringLiteral("pobre"), Qt::CaseInsensitive),
             "P0171 debe contener descripción en español sobre mezcla pobre");

    DtcInfo infoC0035 = dtcMgr.getDtcDetails(QStringLiteral("C0035"));
    QCOMPARE(infoC0035.code, QStringLiteral("C0035"));
    QVERIFY2(infoC0035.descriptionEs.contains(QStringLiteral("rueda"), Qt::CaseInsensitive),
             "C0035 debe contener descripción en español sobre sensor de rueda");

    DtcInfo infoC0200 = dtcMgr.getDtcDetails(QStringLiteral("C0200"));
    QCOMPARE(infoC0200.code, QStringLiteral("C0200"));
    QVERIFY2(infoC0200.descriptionEs.contains(QStringLiteral("rueda"), Qt::CaseInsensitive),
             "C0200 debe contener descripción en español sobre sensor de rueda");
}

void TestObdSimulator::testToyotaCamry2006Simulation()
{
    MockElmDriver mockDriver;
    mockDriver.setVehicleProfile(MockElmDriver::VehicleProfile::ToyotaCamry2006);
    QVERIFY(mockDriver.openConnection());

    DtcManager dtcMgr;
    QVERIFY(dtcMgr.initializeDatabase());

    ObdProtocol obd(&mockDriver, &dtcMgr);
    UdsProtocol uds(&mockDriver, &dtcMgr);

    // 1. Verificación de RPM (0x0BB8 -> 3000 -> 750 RPM)
    double rpm = obd.readRpm();
    QCOMPARE(rpm, 750.0);

    // 2. Verificación de Temperatura Refrigerante (0x82 -> 130 - 40 = 90 °C)
    double coolantTemp = obd.readCoolantTemp();
    QCOMPARE(coolantTemp, 90.0);

    // 3. Verificación de DTCs de Motor / Transmisión (P0171 y P0740)
    QList<DtcInfo> dtcs = obd.readStoredDtcs();
    QStringList codes;
    for (const DtcInfo& d : dtcs) {
        codes.append(d.code);
    }
    QVERIFY2(codes.contains(QStringLiteral("P0171")), "Debe contener código P0171");
    QVERIFY2(codes.contains(QStringLiteral("P0740")), "Debe contener código P0740");

    // 4. Conmutar al módulo ABS Toyota (7B0) y leer código C0200
    QList<DtcInfo> absDtcs = uds.readModuleDtcs(TargetModule::Brakes_ABS_Toyota);
    QStringList absCodes;
    for (const DtcInfo& d : absDtcs) {
        absCodes.append(d.code);
    }
    QVERIFY2(absCodes.contains(QStringLiteral("C0200")), "El módulo ABS Toyota (7B0) debe reportar C0200");
}

void TestObdSimulator::testChevroletBeat2018Simulation()
{
    MockElmDriver mockDriver;
    mockDriver.setVehicleProfile(MockElmDriver::VehicleProfile::ChevroletBeat2018);
    QVERIFY(mockDriver.openConnection());

    DtcManager dtcMgr;
    QVERIFY(dtcMgr.initializeDatabase());

    ObdProtocol obd(&mockDriver, &dtcMgr);
    UdsProtocol uds(&mockDriver, &dtcMgr);

    // 1. Verificación de RPM (0x0C80 -> 3200 / 4 = 800 RPM)
    double rpm = obd.readRpm();
    QCOMPARE(rpm, 800.0);

    // 2. Lectura y reensamblado multiframe del VIN Chevrolet Beat 2018
    QString vin = obd.readVin();
    QCOMPARE(vin, QStringLiteral("MA6CB6CD9JT000001"));

    // 3. Conmutar al módulo ABS Chevrolet GM EBCM (7E2) y leer código C0035
    QList<DtcInfo> absDtcs = uds.readModuleDtcs(TargetModule::Brakes_ABS_ISO);
    QStringList absCodes;
    for (const DtcInfo& d : absDtcs) {
        absCodes.append(d.code);
    }
    QVERIFY2(absCodes.contains(QStringLiteral("C0035")), "El módulo ABS Chevrolet Beat (7E2) debe reportar C0035");
}

QTEST_MAIN(TestObdSimulator)
#include "tst_obd_simulator.moc"

