# OBD2_Scanner_Pro (NissiMech)

Sistema profesional multiplataforma de diagnóstico vehicular y telemetría en tiempo real basado en **C++20**, **Qt6 (LGPLv3)** y **CMake**.

Diseñado bajo la regla de ingeniería **"Cero Suposiciones"**: ningún comando, identificador CAN (`7E0`, `7B0`, etc.), fórmula de decodificación o cabecera física está codificado de manera rígida (*hardcoded*). Todas las definiciones de señales y módulos se leen dinámicamente desde fuentes oficiales de la industria automotriz: **GitHub `OBDb`** (esquema JSON v3) y **GitHub `commaai/opendbc`** (formato Vector CAN `.dbc`).

Compatible con talleres mecánicos, laboratorios de ingeniería automotriz y dispositivos embebidos basados en Linux ARM64 (ej. Raspberry Pi / Toradex) y estaciones de trabajo Windows/Linux x86_64.

---

## 1. Características Principales

- **Arquitectura Dinámica de Telemetría ("Cero Suposiciones"):**
  - **Cargador Nativo `OBDb` (JSON v3):** Parser en C++20 con `QJsonDocument` / `QJsonObject` (100% LGPLv3). Soporta comandos (`hdr`, `rax`, `cmd`), listas de ECUs autorizadas y escalado de señal `fmt` (`bix`, `len`, `mul`, `div`, `add`).
  - **Fórmula Universal de Escalado:** $\text{valor\_físico} = \left(\frac{\text{raw\_bits} \times \text{mul}}{\text{div}}\right) + \text{add}$ con soporte nativo de signo (complemento a dos) y validaciones de rango.
  - **Parser Nativo `commaai/opendbc` (.dbc):** Parser sin dependencias externas (STL + `QRegularExpression`) que procesa definiciones de mensajes `BO_` (convirtiendo IDs a hexadecimal para filtros `AT CRA`) y señales `SG_` con soporte Big-Endian Motorola (`@0`) y Little-Endian Intel (`@1`).
  - **Extractor Universal de Bits (`SignalExtractor`):** Extracción precisa bit a bit para payloads de hasta 64 bits sobre tramas CAN e ISO-TP.

- **Estándares Automotrices Oficiales Soportados:**
  - **SAE J1979 / ISO 15031-5:** Modos de diagnóstico OBD2 `$01`, `$02`, `$03`, `$04`, `$07`, `$09`, `$0A`. Detección segura y automática del protocolo físico activo (`AT SP 0`, `01 00`, `AT DP`, `AT DPN`).
  - **SAE J2012 / ISO 15031-6:** Decodificación bit a bit matemática estricta de Códigos de Diagnóstico de Falla (DTC) según los bits 7 y 6 (`00` $\to$ `P`, `01` $\to$ `C`, `10` $\to$ `B`, `11` $\to$ `U`), decodificando correctamente fallas como `C0035` (`0x40 0x35`) y `C0200` (`0x42 0x00`).
  - **ISO 15765-4 / ISO 15765-2 (ISO-TP):** Reensamblado completo de tramas Single Frame (`0x0N`), First Frame (`0x1N`) y Consecutive Frames (`0x2N`) previo a la extracción de datos de señales y lectura de VIN de 17 caracteres.
  - **ISO 14229-1 (UDS):** Servicio `$19 $02` (ReadDTCInformationByStatusMask) con extracción de Fault Type Byte (FTB) y Status Byte, `$22` (ReadDataByIdentifier) y direccionamiento físico dinámico por ECU (`selectPhysicalModule`).

- **Cortafuegos de Seguridad de ECU (`SafetyGuard`) y Triple Candado KOEO:**
  - **Lista Negra Absoluta:** Bloqueo inquebrantable de modos de escritura, control y reprogramación (`10 02`, `11` ECUReset, `27` SecurityAccess, `28` CommunicationControl, `2E` WriteDataByIdentifier, `2F` InputOutputControl, `31` RoutineControl, `34`/`36`/`37` Flasheo de firmware) y comandos peligrosos del adaptador (`AT BI`, `AT CSM0`, `AT PP`).
  - **Triple Candado de Hardware (Modo 04 / KOEO Ultra-Seguro):** El borrado de DTCs y restablecimiento de monitores de emisiones (`04`) exige tres candados físicos concurrentes antes de autorizar el envío a la ECU:
    1. *Motor apagado:* Régimen menor o igual a 50 RPM (`SAE_ENGINE_RPM` o PID `01 0C`).
    2. *Vehículo detenido:* Velocidad de 0 km/h (`SAE_VEHICLE_SPEED` o PID `01 0D`).
    3. *Batería estable:* Tensión mayor o igual a 11.5 V (`AT RV`) para prevenir corrupción de memorias no volátiles.
  - **Enclavamiento de Borrado:** Bloqueo incondicional si el usuario no otorga confirmación expresa en el cuadro de diálogo.
  - **Lista Blanca Dinámica:** Solo se autorizan cabeceras `AT SH` y comandos de diagnóstico registrados en el perfil vehicular JSON/DBC cargado para la sesión.

- **Conectividad Física de Grado Automotriz y Terminal Crudo:**
  - **Autodetección de Puertos USB (`QSerialPortInfo`):** Detección dinámica y selección prioritaria de adaptadores recomendados (**Vgate vLinker**, **OBDLink**, STN, FTDI, CH340, CP210).
  - **Filtro de Nulos UART:** Supresión automática de bytes nulos (`0x00`) producidos por jitter en el bus RS232/USB del ELM327.
  - **Soporte de Cabeceras Legadas K-Line (ISO 9141-2 / KWP2000):** Supresión de cabeceras de 3 bytes (`48 6B 10` o `80 F1 10`) y omisión del checksum final de 1 byte.
  - **Consola y Log de Tramas Crudas (Raw Terminal Log):** Búfer circular de 300 tramas con timestamps milimétricos `[HH:mm:ss.zzz]`, diferenciación visual de flujo (`TX ->` cyan, `RX <-` verde, `[BLOQUEADO]` rojo, errores y alertas), envío manual validado por `SafetyGuard`, botones de comandos rápidos (`AT DP`, `AT RV`, `01 00`, `01 0C`, `09 02`) y exportación a archivo `.txt`.

- **Cumplimiento Estricto de Licenciamiento Comercial (LGPLv3):**
  - Enlace dinámico (`SHARED`) únicamente con módulos LGPLv3: `Qt6Core`, `Qt6Gui`, `Qt6Qml`, `Qt6Quick`, `Qt6SerialPort`, `Qt6SerialBus`, `Qt6Sql` y `Qt6Test`.
  - Sin dependencias de componentes comerciales como `QtCharts` o `QtGraphs`. Visualización en tiempo real mediante `Canvas` 2D API nativa de QtQuick.
  - Base de datos SQLite3 de dominio público con descripciones clínicas de fallas en español.

---

## 2. Jerarquía de Directorios del Proyecto

```
OBD2_Scanner_Pro/
├── CMakeLists.txt              # Configuración CMake 3.21+ C++20 y módulos LGPLv3
├── README.md                   # Documentación técnica del sistema
├── database/
│   ├── schema.sql              # Esquema DDL y catálogo de códigos DTC
│   ├── dtc_codes_es.sqlite     # Base de datos SQLite con descripciones clínicas en español
│   └── scenarios/              # Escenarios dinámicos para el simulador MockElmDriver
│       ├── toyota_camry_2006_test.json
│       └── chevrolet_beat_2018_test.json
├── opendbc/                    # Fuente de verdad: Perfiles vehiculares OBDb y opendbc
│   ├── SAE-J1979/
│   │   └── signalsets/v3/default.json          # Señales estándar OBD-II legisladas
│   ├── Toyota-Camry/
│   │   ├── signalsets/v3/default.json          # Señales OBDb v3 (Camry 2.4L 2AZ-FE / U250E)
│   │   └── toyota_nodsu_pt_generated.dbc       # Definiciones de mensajes y señales DBC
│   └── Chevrolet-Beat/
│       ├── signalsets/v3/default.json          # Señales OBDb v3 (Chevrolet Beat / Spark)
│       └── gm_global_a_powertrain_generated.dbc# Definiciones de mensajes y señales DBC
├── src/
│   ├── main.cpp                # Punto de entrada, DiagnosticController y enlace QML
│   ├── hal/                    # Capa de Abstracción de Hardware (HAL)
│   │   ├── IObdInterface.h     # Contrato virtual puro de comunicación
│   │   ├── SerialElmDriver.h   # Driver para adaptadores serie ELM327 / STN / vLinker
│   │   ├── SerialElmDriver.cpp
│   │   ├── SocketCanDriver.h   # Driver nativo Linux SocketCAN (ej. can0)
│   │   ├── SocketCanDriver.cpp
│   │   ├── MockElmDriver.h     # Simulador dinámico con carga de escenarios JSON
│   │   └── MockElmDriver.cpp
│   ├── core/                   # Lógica de Diagnóstico y Seguridad
│   │   ├── SafetyGuard.h       # Cortafuegos vehicular contra escrituras accidentales
│   │   ├── SafetyGuard.cpp
│   │   ├── SignalExtractor.h   # Extractor universal de bits (OBDb MSB / DBC Motorola e Intel)
│   │   ├── SignalExtractor.cpp
│   │   ├── ObdbJsonLoader.h    # Cargador de esquemas JSON v3 de OBDb
│   │   ├── ObdbJsonLoader.cpp
│   │   ├── DbcParser.h         # Parser Vector CAN DBC (commaai/opendbc)
│   │   ├── DbcParser.cpp
│   │   ├── ObdProtocol.h       # Protocolo OBD-II, detección de protocolo e ISO-TP
│   │   ├── ObdProtocol.cpp
│   │   ├── UdsProtocol.h       # Protocolo UDS y direccionamiento físico dinámico
│   │   ├── UdsProtocol.cpp
│   │   ├── PidDecoder.h        # Decodificación genérica basada en modelos OBDb
│   │   ├── PidDecoder.cpp
│   │   ├── DtcManager.h        # Decodificación bitwise SAE J2012 y motor SQLite
│   │   └── DtcManager.cpp
│   └── ui/                     # Interfaz Gráfica de Usuario (QtQuick QML)
│       ├── MainDashboard.qml   # Cuadro de instrumentos, velocímetro, perfiles y DTCs
│       └── LiveGraphView.qml   # Osciloscopio en tiempo real renderizado sobre Canvas
└── tests/
    ├── CMakeLists.txt          # Configuración del ejecutable de pruebas
    └── tst_obd_simulator.cpp   # Suite automatizada QtTest (10 suites / 100% passing)
```

---

## 3. Requisitos del Sistema e Instalación de Dependencias

### Compilador y Herramientas Base
- Compilador C++ con soporte C++20 completo: **GCC >= 11** o **Clang >= 13** o **MSVC >= 2019 (v16.10)**.
- **CMake >= 3.21**.
- **Ninja** o **GNU Make**.

### Instalación de Dependencias en Ubuntu / Debian (>= 22.04 LTS / 24.04 LTS)

Ejecuta el siguiente comando para instalar todos los paquetes de desarrollo necesarios de Qt6 y bibliotecas del sistema:

```bash
sudo apt update && sudo apt install -y \
    build-essential \
    cmake \
    g++ \
    libgl-dev \
    libxkbcommon-dev \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-serialport-dev \
    qt6-serialbus-dev \
    libqt6sql6-sqlite \
    libqt6quicklayouts6 \
    libqt6quickcontrols2impl6 \
    qml6-module-qtquick \
    qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts \
    qml6-module-qtquick-templates \
    qml6-module-qtquick-window \
    qml6-module-qtqml-workerscript
```

---

## 4. Comandos de Construcción, Pruebas y Ejecución

### 4.1. Configuración y Compilación del Proyecto

Desde la raíz del repositorio (`NissiMech/`), genera el árbol de construcción con CMake y compila en modo Release:

```bash
# 1. Configurar el proyecto con CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 2. Compilar el ejecutable principal y las pruebas unitarias
cmake --build build -j$(nproc)
```

Al compilar, CMake copiará automáticamente los directorios `database/` y `opendbc/` a la carpeta de salida `build/` para garantizar la resolución inmediata de rutas.

---

### 4.2. Ejecución de Pruebas Unitarias y Simulación Dinámica

El proyecto incluye una batería completa de pruebas unitarias implementada con `QtTest` en `tests/tst_obd_simulator.cpp`. Valida el cortafuegos `SafetyGuard`, decodificación bitwise SAE J2012, extracción de señales OBDb/DBC, reensamblado multiframe ISO-TP y escenarios vehiculares dinámicos del Toyota Camry 2006 y Chevrolet Beat 2018.

#### Opción A: Ejecución estándar con CTest (Recomendado para CI/CD)
```bash
ctest --test-dir build --output-on-failure
```

#### Opción B: Ejecución directa con salida detallada por aserción
```bash
./build/tests/tst_obd_simulator -v2
```

Salida esperada (100% de pruebas aprobadas):
```text
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 158ms
********* Finished testing of TestObdSimulator *********
```

---

### 4.3. Ejecución de la Aplicación

#### En entorno con Servidor Gráfico Activo (Escritorio Linux / X11 / Wayland / Windows):
Lanza la aplicación directamente desde la raíz o desde la carpeta `build/`:

```bash
./build/OBD2_Scanner_Pro
```

#### En entorno Headless / Servidor / WSL2 sin servidor X11 (Modo Offscreen):
Si ejecutas en un entorno sin pantalla o terminal remota sin display gráfico:

```bash
./build/OBD2_Scanner_Pro -platform offscreen
```

---

### 4.4. Uso con Entornos de Dependencias Portables / Sandboxes Locales

Si estás construyendo o ejecutando en un entorno con dependencias de Qt6 instaladas en un prefijo no estándar (como `/tmp/qt6_deps`), exporta las siguientes variables de entorno antes de compilar o ejecutar:

```bash
# 1. Configurar variables de entorno para herramientas, bibliotecas y plugins de Qt6
export PATH=/tmp/qt6_deps/usr/bin:$PATH
export LD_LIBRARY_PATH=/tmp/qt6_deps/usr/lib/x86_64-linux-gnu:/tmp/qt6_deps/usr/lib:$LD_LIBRARY_PATH
export QT_PLUGIN_PATH=/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/qt6/plugins
export QML_IMPORT_PATH=/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/qt6/qml

# 2. Compilar
cmake --build build -j$(nproc)

# 3. Ejecutar pruebas
ctest --test-dir build --output-on-failure

# 4. Lanzar la aplicación
./build/OBD2_Scanner_Pro
```

---

## 5. Hardware y Adaptadores Soportados

1. **Vgate vLinker FS USB / OBDLink EX USB:**
   - Conexión vía puerto serie (`/dev/ttyUSB0` o `COMx`).
   - Soporte para chipsets STN y ELM327 estándar.
   - Baudrates recomendados: `115200` bps o `500000` bps (para adaptadores basados en STN).
2. **Interfaces Nativas Linux SocketCAN:**
   - Interfaces USB-CAN (ej. CANable con firmware candleLight, PEAK PCAN-USB, etc.).
   - Se conectan directamente seleccionando la interfaz de red CAN del sistema (ej. `can0`).
   - Configuración típica del bus en Linux:
     ```bash
     sudo ip link set can0 type can bitrate 500000
     sudo ip link set up can0
     ```
3. **Simulador Interno (`MockElmDriver`):**
   - No requiere hardware físico conectado.
   - Carga cualquier escenario de prueba en formato JSON ubicado en `database/scenarios/`.
   - Devuelve respuestas reales grabadas y rechaza con `"NO DATA"` cualquier comando fuera de especificación.

---

## 6. Licencia

Este proyecto está licenciado bajo la **GNU Lesser General Public License v3.0 (LGPLv3)**. Todos los módulos y bibliotecas de enlace dinámico cumplen estrictamente con las directrices de licenciamiento libre de Qt6.

