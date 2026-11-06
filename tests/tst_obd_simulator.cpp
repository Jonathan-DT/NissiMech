#include <QtTest>
#include "../src/hal/MockElmDriver.h"
#include "../src/core/SafetyGuard.h"
#include "../src/core/SignalExtractor.h"
#include "../src/core/ObdbJsonLoader.h"
#include "../src/core/DbcParser.h"
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
    void testSignalExtractor();
    void testObdbJsonLoader();
    void testDbcParser();
    void testToyotaCamry2006DynamicSimulation();
    void testChevroletBeat2018DynamicSimulation();
};

void TestObdSimulator::initTestCase()
{
    DtcManager mgr;
    QVERIFY(mgr.initializeDatabase());
}

void TestObdSimulator::testSafetyGuard()
{
    // Limpiar contexto previo
    SafetyGuard::clearAuthorizedContext();

    // Comandos de configuración AT seguros siempre aprobados
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("AT Z")), "AT Z debe ser aprobado");
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("AT SP 0")), "AT SP 0 debe ser aprobado");
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("AT DP")), "AT DP debe ser aprobado");
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("01 00")), "Handshake 01 00 debe ser aprobado");

    // Sin contexto vehicular activo, comandos de diagnóstico específicos deben ser bloqueados
    QString blockReason;
    bool diagBlocked = SafetyGuard::isCommandSafe(QStringLiteral("01 0C"), false, &blockReason);
    QVERIFY2(!diagBlocked, "Sin perfil vehicular activo, 01 0C debe ser bloqueado por defecto");
    QVERIFY2(!blockReason.isEmpty(), "Debe existir razón de bloqueo");

    // Borrado de DTC (Modo 04): rechazado por defecto sin confirmación
    bool safeUnconfirmed = SafetyGuard::isCommandSafe(QStringLiteral("04"), false, &blockReason);
    QVERIFY2(!safeUnconfirmed, "El comando 04 debe ser bloqueado sin confirmación");

    // Borrado de DTC (Modo 04): aprobado con confirmación explícita
    bool safeConfirmed = SafetyGuard::isCommandSafe(QStringLiteral("04"), true);
    QVERIFY2(safeConfirmed, "El comando 04 debe ser aprobado con confirmación expresa");

    // Lista negra permanente: comandos peligrosos bloqueados siempre
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("AT BI")), "AT BI debe ser bloqueado siempre");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("AT CSM0")), "AT CSM0 debe ser bloqueado siempre");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("27 01")), "UDS SecurityAccess (27 01) debe ser bloqueado siempre");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("2E F1 90")), "UDS WriteDataByIdentifier (2E F1 90) debe ser bloqueado siempre");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("10 02")), "UDS ProgrammingSession (10 02) debe ser bloqueado siempre");

    // Configurar contexto autorizado
    QSet<QString> cmds = { QStringLiteral("01 0C"), QStringLiteral("21 01"), QStringLiteral("09 02") };
    QSet<QString> hdrs = { QStringLiteral("7DF"), QStringLiteral("7E0") };
    QSet<QString> cra = { QStringLiteral("7E8") };
    SafetyGuard::setAuthorizedContext(cmds, hdrs, cra);

    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("01 0C")), "01 0C debe ser aprobado con contexto activo");
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("21 01")), "21 01 debe ser aprobado con contexto activo");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("22 99 99")), "Comando no registrado debe ser bloqueado");
    QVERIFY2(SafetyGuard::isCommandSafe(QStringLiteral("AT SH 7E0")), "AT SH 7E0 debe ser aprobado");
    QVERIFY2(!SafetyGuard::isCommandSafe(QStringLiteral("AT SH 123")), "AT SH con header no registrado debe ser bloqueado");
}

void TestObdSimulator::testDtcBitwiseDecoding()
{
    // Verificación matemática estricta SAE J2012 Powertrain (00 -> 'P')
    QString p0171 = DtcManager::decodeDtcPair(0x01, 0x71);
    QCOMPARE(p0171, QStringLiteral("P0171"));

    QString p0740 = DtcManager::decodeDtcPair(0x07, 0x40);
    QCOMPARE(p0740, QStringLiteral("P0740"));

    // Verificación de decodificación Chassis (01 -> 'C') en estándar real SAE J2012
    // C0035 = 0x40 (0100 0000 -> 'C' + 0 + 0) y 0x35
    QString c0035 = DtcManager::decodeDtcPair(0x40, 0x35);
    QCOMPARE(c0035, QStringLiteral("C0035"));

    // C0200 = 0x42 (0100 0010 -> 'C' + 0 + 2) y 0x00
    QString c0200 = DtcManager::decodeDtcPair(0x42, 0x00);
    QCOMPARE(c0200, QStringLiteral("C0200"));

    // UDS 19 02 Parseo sin parches falsos
    DtcManager dtcMgr;
    QVERIFY(dtcMgr.initializeDatabase());

    QString rawUdsToyota = QStringLiteral("7B8 06 59 02 FF 42 00 00 2F \r\r>");
    QList<DtcInfo> dtcsToyota = dtcMgr.parseDtcResponse(rawUdsToyota);
    QCOMPARE(dtcsToyota.size(), 1);
    QCOMPARE(dtcsToyota.first().code, QStringLiteral("C0200"));
    QCOMPARE(dtcsToyota.first().faultType, QStringLiteral("00"));
    QCOMPARE(dtcsToyota.first().statusByte, static_cast<uint8_t>(0x2F));

    QString rawUdsBeat = QStringLiteral("7EA 06 59 02 FF 40 35 00 2F \r\r>");
    QList<DtcInfo> dtcsBeat = dtcMgr.parseDtcResponse(rawUdsBeat);
    QCOMPARE(dtcsBeat.size(), 1);
    QCOMPARE(dtcsBeat.first().code, QStringLiteral("C0035"));
}

void TestObdSimulator::testSignalExtractor()
{
    // 1. Prueba de extracción de señal OBDb (Big Endian universal)
    // Ejemplo: RPM en bytes 0x0B, 0xB8 (3000 -> 3000 / 4 = 750 RPM)
    QByteArray obdbPayload;
    obdbPayload.append(static_cast<char>(0x0B));
    obdbPayload.append(static_cast<char>(0xB8));

    ObdbSignalFormat rpmFmt;
    rpmFmt.bix = 0;
    rpmFmt.len = 16;
    rpmFmt.div = 4.0;
    auto rpmVal = SignalExtractor::extractObdbValue(obdbPayload, rpmFmt);
    QVERIFY(rpmVal.has_value());
    QCOMPARE(*rpmVal, 750.0);

    // Ejemplo con offset y multiplicación: Coolant 0x82 (130 -> 130 - 40 = 90 °C)
    QByteArray ectPayload;
    ectPayload.append(static_cast<char>(0x82));
    ObdbSignalFormat ectFmt;
    ectFmt.bix = 0;
    ectFmt.len = 8;
    ectFmt.add = -40.0;
    auto ectVal = SignalExtractor::extractObdbValue(ectPayload, ectFmt);
    QVERIFY(ectVal.has_value());
    QCOMPARE(*ectVal, 90.0);

    // 2. Prueba de extracción de señal DBC Motorola (@0)
    // BO_ 170 WHEEL_SPEEDS: 8
    // SG_ WHEEL_SPEED_FR : 6|15@0+ (0.01,-67.67) "km/h"
    // Supongamos que raw_bits = 16767 -> (16767 * 0.01) - 67.67 = 100.00 km/h
    // 16767 = 0x417F (15 bits: MSB en bit 6 de byte 0, LSB en bit 0 de byte 1)
    // Byte 0: bits 6..0 = 0x41 (0100 0001), Byte 1: bits 7..0 = 0x7F (0111 1111)
    QByteArray canFrame;
    canFrame.resize(8);
    canFrame[0] = static_cast<char>(0x41);
    canFrame[1] = static_cast<char>(0x7F);

    DbcSignalFormat dbcMotorolaFmt;
    dbcMotorolaFmt.startBit = 6;
    dbcMotorolaFmt.bitLength = 15;
    dbcMotorolaFmt.isLittleEndian = false;
    dbcMotorolaFmt.isSigned = false;
    dbcMotorolaFmt.scale = 0.01;
    dbcMotorolaFmt.offset = -67.67;

    auto wsVal = SignalExtractor::extractDbcValue(canFrame, dbcMotorolaFmt);
    QVERIFY(wsVal.has_value());
    QVERIFY(std::abs(*wsVal - 100.0) < 0.01);
}

void TestObdSimulator::testObdbJsonLoader()
{
    ObdbJsonLoader loader;
    QVERIFY2(loader.loadFile(QStringLiteral("opendbc/SAE-J1979/signalsets/v3/default.json")),
             "Debe cargar exitosamente default.json de SAE-J1979");

    const ObdbSignal* rpmSig = loader.findSignal(QStringLiteral("SAE_ENGINE_RPM"));
    QVERIFY(rpmSig != nullptr);
    QCOMPARE(rpmSig->fmt.div, 4.0);

    const ObdbCommand* cmd = loader.findCommandForSignal(QStringLiteral("SAE_ENGINE_RPM"));
    QVERIFY(cmd != nullptr);
    QCOMPARE(cmd->service, QStringLiteral("01"));
    QCOMPARE(cmd->pidHex, QStringLiteral("0C"));

    QVERIFY(loader.authorizedCommands().contains(QStringLiteral("01 0C")));
}

void TestObdSimulator::testDbcParser()
{
    DbcParser parser;
    QVERIFY2(parser.loadFile(QStringLiteral("opendbc/Toyota-Camry/toyota_nodsu_pt_generated.dbc")),
             "Debe cargar el DBC generado para Toyota Camry");

    const DbcMessage* msg = parser.findMessageById(170);
    QVERIFY(msg != nullptr);
    QCOMPARE(msg->name, QStringLiteral("WHEEL_SPEEDS"));
    QCOMPARE(msg->idHex, QStringLiteral("0AA"));

    QVERIFY(parser.authorizedHexIds().contains(QStringLiteral("0AA")));
}

void TestObdSimulator::testToyotaCamry2006DynamicSimulation()
{
    MockElmDriver mockDriver;
    QVERIFY(mockDriver.loadScenario(QStringLiteral("toyota_camry_2006_test.json")));
    QVERIFY(mockDriver.openConnection());

    DtcManager dtcMgr;
    QVERIFY(dtcMgr.initializeDatabase());

    // Fusión de SAE-J1979 base y perfil específico Camry 2006
    ObdbJsonLoader obdbLoader;
    QVERIFY(obdbLoader.loadFile(QStringLiteral("opendbc/SAE-J1979/signalsets/v3/default.json")));
    QVERIFY(obdbLoader.appendFile(QStringLiteral("opendbc/Toyota-Camry/signalsets/v3/default.json"), 2006));

    DbcParser dbcParser;
    QVERIFY(dbcParser.loadFile(QStringLiteral("opendbc/Toyota-Camry/toyota_nodsu_pt_generated.dbc")));

    // Configurar SafetyGuard para la sesión activa
    SafetyGuard::setAuthorizedContext(obdbLoader.authorizedCommands(),
                                     obdbLoader.authorizedHeaders(),
                                     obdbLoader.authorizedCraFilters() + dbcParser.authorizedHexIds());

    ObdProtocol obd(&mockDriver, &dtcMgr, &obdbLoader, &dbcParser);
    UdsProtocol uds(&mockDriver, &dtcMgr, &obdbLoader, &dbcParser);

    // 1. Detección segura de protocolo físico
    QString proto = obd.detectActiveProtocol();
    QVERIFY(proto.contains(QStringLiteral("ISO 15765-4")));

    // 2. Consulta dinámica de RPM (750 RPM)
    auto rpm = obd.querySignal(QStringLiteral("CAMRY_ENGINE_SPEED_V2"));
    QVERIFY(rpm.has_value());
    QCOMPARE(*rpm, 750.0);

    // 3. Consulta dinámica de ECT (90 °C)
    auto ect = obd.querySignal(QStringLiteral("CAMRY_COOLANT_TEMP_V2"));
    QVERIFY(ect.has_value());
    QCOMPARE(*ect, 90.0);

    // 4. Consulta de voltaje de batería (CAMRY_BATTERY_VOLTAGE_V2 en 21 01 a 27 bytes de datos = 13.8V)
    auto batt = obd.querySignal(QStringLiteral("CAMRY_BATTERY_VOLTAGE_V2"));
    QVERIFY(batt.has_value());
    QCOMPARE(*batt, 13.8);

    // 5. Consulta de temperatura ATF de transmisión (21 82 = 70 °C)
    auto atf = obd.querySignal(QStringLiteral("CAMRY_ATF_PAN_T"));
    QVERIFY(atf.has_value());
    QCOMPARE(*atf, 70.0);

    // 6. Consulta de velocidad de rueda ABS según OBDb oficial (7B0 con 21 03 = 64.0 km/h)
    auto ws = obd.querySignal(QStringLiteral("CAMRY_TIRE_FR_SPD"));
    QVERIFY(ws.has_value());
    QCOMPARE(*ws, 64.0);

    // 7. Verificación de códigos de falla SAE J1979
    QList<DtcInfo> dtcs = obd.readStoredDtcs();
    QStringList codes;
    for (const auto& d : dtcs) codes.append(d.code);
    QVERIFY(codes.contains(QStringLiteral("P0171")));
    QVERIFY(codes.contains(QStringLiteral("P0740")));
}

void TestObdSimulator::testChevroletBeat2018DynamicSimulation()
{
    MockElmDriver mockDriver;
    QVERIFY(mockDriver.loadScenario(QStringLiteral("chevrolet_beat_2018_test.json")));
    QVERIFY(mockDriver.openConnection());

    DtcManager dtcMgr;
    QVERIFY(dtcMgr.initializeDatabase());

    // Fusión de SAE-J1979 base y perfil específico Chevrolet Beat 2018
    ObdbJsonLoader obdbLoader;
    QVERIFY(obdbLoader.loadFile(QStringLiteral("opendbc/SAE-J1979/signalsets/v3/default.json")));
    QVERIFY(obdbLoader.appendFile(QStringLiteral("opendbc/Chevrolet-Beat/signalsets/v3/default.json"), 2018));

    DbcParser dbcParser;
    QVERIFY(dbcParser.loadFile(QStringLiteral("opendbc/Chevrolet-Beat/gm_global_a_powertrain_generated.dbc")));

    SafetyGuard::setAuthorizedContext(obdbLoader.authorizedCommands(),
                                     obdbLoader.authorizedHeaders(),
                                     obdbLoader.authorizedCraFilters() + dbcParser.authorizedHexIds());

    ObdProtocol obd(&mockDriver, &dtcMgr, &obdbLoader, &dbcParser);
    UdsProtocol uds(&mockDriver, &dtcMgr, &obdbLoader, &dbcParser);

    // 1. Consulta de RPM base estándar legislada SAE J1979 (01 0C = 800 RPM)
    auto saeRpm = obd.querySignal(QStringLiteral("SAE_ENGINE_RPM"));
    QVERIFY(saeRpm.has_value());
    QCOMPARE(*saeRpm, 800.0);

    // 2. Lectura y reensamblado multiframe ISO-TP del VIN
    QString vin = obd.readVin();
    QCOMPARE(vin, QStringLiteral("MA6CB6CD9JT000001"));

    // 3. Consulta UDS de fallas en módulo GM EBCM (7E2)
    QList<DtcInfo> absDtcs = uds.readModuleDtcs(QStringLiteral("7E2"), QStringLiteral("7EA"));
    QStringList absCodes;
    for (const auto& d : absDtcs) absCodes.append(d.code);
    QVERIFY(absCodes.contains(QStringLiteral("C0035")));

    // 4. Comando no documentado en el escenario debe devolver NO DATA
    QString resp = mockDriver.sendRawCommand(QStringLiteral("AT DP"));
    QVERIFY(!resp.isEmpty());

    // Comando no presente en el escenario
    SafetyGuard::setAuthorizedContext(obdbLoader.authorizedCommands() + QSet<QString>{QStringLiteral("22 FFFF")},
                                     obdbLoader.authorizedHeaders(),
                                     obdbLoader.authorizedCraFilters());
    QString unkResp = mockDriver.sendRawCommand(QStringLiteral("22 FF FF"));
    QCOMPARE(unkResp, QStringLiteral("NO DATA\r\r>"));
}

QTEST_MAIN(TestObdSimulator)
#include "tst_obd_simulator.moc"
