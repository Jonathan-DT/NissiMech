-- Esquema de Base de Datos para Códigos de Diagnóstico de Falla (DTC)
-- Estándares: SAE J2012 / ISO 15031-6
-- Motor de Base de Datos: SQLite3 (Dominio Público) compatible con QtSql (QSQLITE)

CREATE TABLE IF NOT EXISTS dtc_codes (
    code TEXT PRIMARY KEY,
    system TEXT NOT NULL,
    description_es TEXT NOT NULL,
    severity INTEGER NOT NULL DEFAULT 1
);

-- Índices para búsqueda rápida
CREATE INDEX IF NOT EXISTS idx_dtc_system ON dtc_codes(system);

-- Inserción de códigos de falla oficiales requeridos y estándares (Español)
INSERT OR REPLACE INTO dtc_codes (code, system, description_es, severity) VALUES
('P0171', 'Powertrain (Motor)', 'Sistema demasiado pobre (Banco 1)', 2),
('P0300', 'Powertrain (Motor)', 'Fallo de encendido detectado en cilindros múltiples / aleatorios', 2),
('P0700', 'Powertrain (Transmisión)', 'Sistema de control de transmisión (Solicitud MIL)', 2),
('P0740', 'Powertrain (Transmisión)', 'Mal funcionamiento en circuito del embrague del convertidor de par (TCC)', 2),
('C0035', 'Chassis (Frenos ABS)', 'Circuito del sensor de velocidad de rueda delantera izquierda', 2),
('C0200', 'Chassis (Frenos ABS)', 'Falla en sensor de velocidad de rueda delantera derecha', 2),
('U0100', 'Network (Red de Comunicación)', 'Pérdida de comunicación con el Módulo de Control del Motor (ECM/PCM)', 3),
('U0121', 'Network (Red de Comunicación)', 'Pérdida de comunicación con el Módulo de Control de Frenos ABS', 3),
('P0118', 'Powertrain (Motor)', 'Circuito del sensor de temperatura de refrigerante del motor (ECT) entrada alta', 1),
('P0102', 'Powertrain (Motor)', 'Circuito de flujo de masa de aire (MAF) entrada baja', 2),
('P0420', 'Powertrain (Motor)', 'Eficiencia del sistema catalizador por debajo del umbral (Banco 1)', 2),
('P0500', 'Powertrain (Velocidad)', 'Falla en el sensor de velocidad del vehículo (VSS)', 2),
('B0001', 'Body (Airbags / Seguridad)', 'Circuito de control de despliegue del airbag del conductor etapa 1', 3),
('U0001', 'Network (Bus CAN)', 'Bus de comunicación CAN de alta velocidad', 3);

