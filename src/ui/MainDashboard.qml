import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * MainDashboard.qml
 * Tablero principal de diagnóstico automotriz profesional para OBD2_Scanner_Pro (NissiMech).
 * 100% Desacoplado: Poblado dinámicamente desde perfiles OBDb JSON y Vector DBC en opendbc/.
 */
ApplicationWindow {
    id: appWindow
    visible: true
    width: 1200
    height: 800
    title: "OBD2 Scanner Pro - Diagnóstico Automotriz Dinámico (OBDb / commaai opendbc)"
    color: "#0a0e14"

    // Barra de Navegación y Conexión Superior
    header: ToolBar {
        background: Rectangle {
            color: "#121824"
            border.color: "#1e293b"
            border.width: 1
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12

            // Indicador de Estado
            Rectangle {
                width: 14
                height: 14
                radius: 7
                color: diagnosticController.connected ? "#00e676" : "#ff1744"

                SequentialAnimation on opacity {
                    running: diagnosticController.connected
                    loops: Animation.Infinite
                    PropertyAnimation { to: 0.4; duration: 800 }
                    PropertyAnimation { to: 1.0; duration: 800 }
                }
            }

            Text {
                text: "OBD2 SCANNER PRO"
                color: "#f8fafc"
                font.bold: true
                font.pixelSize: 15
                font.letterSpacing: 1.5
            }

            Rectangle { width: 1; height: 24; color: "#334155" }

            // Selector de Perfil Vehicular Dinámico (opendbc/)
            Text {
                text: "Vehículo:"
                color: "#94a3b8"
                font.pixelSize: 13
            }

            ComboBox {
                id: vehicleSelector
                model: diagnosticController.availableVehicles
                currentIndex: diagnosticController.selectedVehicleIndex
                onActivated: function(index) {
                    diagnosticController.selectedVehicleIndex = index;
                }
                Layout.preferredWidth: 200
            }

            Rectangle { width: 1; height: 24; color: "#334155" }

            // Selector de Interfaz / Hardware
            Text {
                text: "Interfaz:"
                color: "#94a3b8"
                font.pixelSize: 13
            }

            ComboBox {
                id: adapterSelector
                model: diagnosticController.availableAdapters
                currentIndex: diagnosticController.selectedAdapterIndex
                onActivated: function(index) {
                    diagnosticController.selectedAdapterIndex = index;
                }
                Layout.preferredWidth: 220
            }

            ComboBox {
                id: serialPortSelector
                visible: adapterSelector.currentIndex === 1
                Layout.preferredWidth: 240
                model: diagnosticController.availableSerialPorts
                displayText: {
                    if (count === 0) return "Sin puertos USB detectados";
                    if (currentIndex >= 0 && currentIndex < count) {
                        var item = model[currentIndex];
                        if (item) {
                            var name = item.portName || "";
                            var desc = item.description || "";
                            var rec = item.isRecommended ? " ★" : "";
                            return name + (desc ? (" - " + desc) : "") + rec;
                        }
                    }
                    return diagnosticController.selectedSerialPort || "Seleccionar puerto USB";
                }
                delegate: ItemDelegate {
                    width: serialPortSelector.width
                    contentItem: Text {
                        text: {
                            var name = modelData.portName || "";
                            var desc = modelData.description || "";
                            var rec = modelData.isRecommended ? " ★ [Recomendado]" : "";
                            return name + (desc ? (" - " + desc) : "") + rec;
                        }
                        color: modelData.isRecommended ? "#38bdf8" : "#f8fafc"
                        font.bold: modelData.isRecommended
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: highlighted ? "#1e293b" : "#0f172a"
                    }
                }
                onActivated: function(index) {
                    if (index >= 0 && index < model.length) {
                        var item = model[index];
                        diagnosticController.selectedSerialPort = item.systemLocation || item.portName;
                    }
                }
            }

            Button {
                id: refreshPortsBtn
                text: "Refrescar Puertos"
                visible: adapterSelector.currentIndex === 1
                onClicked: diagnosticController.refreshSerialPorts()
            }

            TextField {
                id: canPortField
                placeholderText: "can0"
                text: "can0"
                visible: adapterSelector.currentIndex === 2
                Layout.preferredWidth: 100
            }

            Button {
                id: connectBtn
                text: diagnosticController.connected ? "Desconectar" : "Conectar"
                highlighted: !diagnosticController.connected
                onClicked: {
                    if (diagnosticController.connected) {
                        diagnosticController.disconnectAdapter();
                    } else {
                        if (adapterSelector.currentIndex === 1) {
                            diagnosticController.connectAdapter(diagnosticController.selectedSerialPort);
                        } else if (adapterSelector.currentIndex === 2) {
                            diagnosticController.connectAdapter(canPortField.text);
                        } else {
                            diagnosticController.connectAdapter();
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Tensión de Batería
            Rectangle {
                color: "#1e293b"
                radius: 6
                Layout.preferredHeight: 32
                Layout.preferredWidth: 110

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "BAT:"; color: "#94a3b8"; font.bold: true; font.pixelSize: 12 }
                    Text {
                        text: diagnosticController.batteryVoltage.toFixed(1) + " V"
                        color: "#fbbf24"
                        font.bold: true
                        font.pixelSize: 14
                        font.family: "monospace"
                    }
                }
            }
        }
    }

    // Contenido Central en Pestañas
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Barra informativa de protocolo negociado (detectActiveProtocol)
        Rectangle {
            Layout.fillWidth: true
            height: 36
            color: "#0f172a"
            border.color: "#1e293b"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 16

                Text {
                    text: "Protocolo Físico Activo:"
                    color: "#94a3b8"
                    font.bold: true
                    font.pixelSize: 12
                }

                Text {
                    text: diagnosticController.activeProtocol
                    color: "#38bdf8"
                    font.bold: true
                    font.pixelSize: 13
                    font.family: "monospace"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Perfil Cargado: " + diagnosticController.activeVehicleName
                    color: "#a855f7"
                    font.pixelSize: 12
                }

                Button {
                    text: "Detectar Protocolo (AT SP 0)"
                    enabled: diagnosticController.connected
                    onClicked: diagnosticController.detectVehicleProtocol()
                }
            }
        }

        TabBar {
            id: mainTabBar
            Layout.fillWidth: true
            background: Rectangle { color: "#0d131d" }

            TabButton { text: "Medidores en Vivo"; width: implicitWidth + 24 }
            TabButton { text: "Osciloscopio / Gráficas"; width: implicitWidth + 24 }
            TabButton { text: "Códigos de Falla (DTC)"; width: implicitWidth + 24 }
            TabButton { text: "Consola / Log Crudo"; width: implicitWidth + 24 }
        }

        StackLayout {
            id: stackLayout
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: mainTabBar.currentIndex

            // =================================================================
            // VISTA 1: MEDIDORES EN VIVO
            // =================================================================
            ScrollView {
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 20

                    // Ficha de Identificación del Vehículo
                    Rectangle {
                        Layout.fillWidth: true
                        height: 50
                        color: "#161f2e"
                        radius: 8
                        border.color: "#253347"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 16

                            Text {
                                text: "N° Bastidor / VIN:"
                                color: "#94a3b8"
                                font.bold: true
                                font.pixelSize: 13
                            }
                            Text {
                                text: diagnosticController.vin.length > 0 ? diagnosticController.vin : "No consultado (requiere conexión)"
                                color: "#38bdf8"
                                font.bold: true
                                font.pixelSize: 14
                                font.family: "monospace"
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "Leer VIN (09 02)"
                                enabled: diagnosticController.connected
                                onClicked: diagnosticController.pollVin()
                            }
                        }
                    }

                    // Cuadrícula de Relojes y Medidores
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 16
                        rowSpacing: 16

                        // Medidor: RPM Motor
                        Rectangle {
                            Layout.fillWidth: true
                            height: 180
                            color: "#131b27"
                            radius: 10
                            border.color: "#223145"

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 8

                                Text {
                                    text: "RÉGIMEN DE MOTOR"
                                    color: "#94a3b8"
                                    font.bold: true
                                    font.pixelSize: 12
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Text {
                                    text: diagnosticController.rpm.toFixed(0)
                                    color: "#00e5ff"
                                    font.bold: true
                                    font.pixelSize: 42
                                    font.family: "monospace"
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Text {
                                    text: "RPM"
                                    color: "#64748b"
                                    font.pixelSize: 14
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                ProgressBar {
                                    value: diagnosticController.rpm / 7000.0
                                    Layout.preferredWidth: 200
                                }
                            }
                        }

                        // Medidor: Velocidad del Vehículo
                        Rectangle {
                            Layout.fillWidth: true
                            height: 180
                            color: "#131b27"
                            radius: 10
                            border.color: "#223145"

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 8

                                Text {
                                    text: "VELOCIDAD"
                                    color: "#94a3b8"
                                    font.bold: true
                                    font.pixelSize: 12
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Text {
                                    text: diagnosticController.speed.toFixed(0)
                                    color: "#00e676"
                                    font.bold: true
                                    font.pixelSize: 42
                                    font.family: "monospace"
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Text {
                                    text: "km / h"
                                    color: "#64748b"
                                    font.pixelSize: 14
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                ProgressBar {
                                    value: diagnosticController.speed / 240.0
                                    Layout.preferredWidth: 200
                                }
                            }
                        }

                        // Medidor: Temperatura de Refrigerante (ECT)
                        Rectangle {
                            Layout.fillWidth: true
                            height: 180
                            color: "#131b27"
                            radius: 10
                            border.color: "#223145"

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 8

                                Text {
                                    text: "REFRIGERANTE (ECT)"
                                    color: "#94a3b8"
                                    font.bold: true
                                    font.pixelSize: 12
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Text {
                                    text: diagnosticController.coolantTemp.toFixed(1) + " °C"
                                    color: diagnosticController.coolantTemp > 105 ? "#ff1744" : "#ff9100"
                                    font.bold: true
                                    font.pixelSize: 36
                                    font.family: "monospace"
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Text {
                                    text: "Normal: 85 - 100 °C"
                                    color: "#64748b"
                                    font.pixelSize: 12
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        // Medidor Secundario: Flujo MAF
                        Rectangle {
                            Layout.fillWidth: true
                            height: 120
                            color: "#131b27"
                            radius: 10
                            border.color: "#223145"

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 4
                                Text { text: "MASA DE AIRE (MAF)"; color: "#94a3b8"; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                Text { text: diagnosticController.maf.toFixed(2) + " g/s"; color: "#c084fc"; font.pixelSize: 24; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            }
                        }

                        // Medidor Secundario: Posición Mariposa (TPS) / Carga
                        Rectangle {
                            Layout.fillWidth: true
                            height: 120
                            color: "#131b27"
                            radius: 10
                            border.color: "#223145"

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 4
                                Text { text: "POSICIÓN ACELERADOR / CARGA"; color: "#94a3b8"; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                Text { text: diagnosticController.throttle.toFixed(1) + " %"; color: "#38bdf8"; font.pixelSize: 24; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            }
                        }

                        // Medidor Secundario: Temp Aire Admisión
                        Rectangle {
                            Layout.fillWidth: true
                            height: 120
                            color: "#131b27"
                            radius: 10
                            border.color: "#223145"

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 4
                                Text { text: "AIRE ADMISIÓN (IAT)"; color: "#94a3b8"; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                Text { text: diagnosticController.intakeAirTemp.toFixed(1) + " °C"; color: "#facc15"; font.pixelSize: 24; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            }
                        }
                    }
                }
            }

            // =================================================================
            // VISTA 2: OSCILOSCOPIO Y GRÁFICAS EN TIEMPO REAL (CANVAS LGPLv3)
            // =================================================================
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text { text: "Canal a Graficar:"; color: "#94a3b8"; font.bold: true; font.pixelSize: 13 }

                        Button {
                            text: "RPM Motor"
                            highlighted: liveGraph.channelName === "RPM del Motor"
                            onClicked: {
                                liveGraph.channelName = "RPM del Motor";
                                liveGraph.unit = "RPM";
                                liveGraph.minValue = 0;
                                liveGraph.maxValue = 6000;
                                liveGraph.lineColor = "#00e5ff";
                                liveGraph.glowColor = "#3300e5ff";
                            }
                        }

                        Button {
                            text: "Velocidad"
                            highlighted: liveGraph.channelName === "Velocidad del Vehículo"
                            onClicked: {
                                liveGraph.channelName = "Velocidad del Vehículo";
                                liveGraph.unit = "km/h";
                                liveGraph.minValue = 0;
                                liveGraph.maxValue = 200;
                                liveGraph.lineColor = "#00e676";
                                liveGraph.glowColor = "#3300e676";
                            }
                        }

                        Button {
                            text: "Temp Refrigerante"
                            highlighted: liveGraph.channelName === "Temperatura de Refrigerante"
                            onClicked: {
                                liveGraph.channelName = "Temperatura de Refrigerante";
                                liveGraph.unit = "°C";
                                liveGraph.minValue = 0;
                                liveGraph.maxValue = 130;
                                liveGraph.lineColor = "#ff9100";
                                liveGraph.glowColor = "#33ff9100";
                            }
                        }

                        Button {
                            text: "Voltaje Batería"
                            highlighted: liveGraph.channelName === "Tensión de Batería"
                            onClicked: {
                                liveGraph.channelName = "Tensión de Batería";
                                liveGraph.unit = "V";
                                liveGraph.minValue = 10;
                                liveGraph.maxValue = 16;
                                liveGraph.lineColor = "#ffd600";
                                liveGraph.glowColor = "#33ffd600";
                            }
                        }
                    }

                    LiveGraphView {
                        id: liveGraph
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        currentValue: {
                            if (channelName === "RPM del Motor") return diagnosticController.rpm;
                            if (channelName === "Velocidad del Vehículo") return diagnosticController.speed;
                            if (channelName === "Temperatura de Refrigerante") return diagnosticController.coolantTemp;
                            return diagnosticController.batteryVoltage;
                        }
                    }
                }
            }

            // =================================================================
            // VISTA 3: GESTIÓN DE CÓDIGOS DE FALLA (DTC)
            // =================================================================
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    // Botonera de Consulta Dinámica
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Button {
                            text: "Escanear Fallas Almacenadas (Modo 03)"
                            enabled: diagnosticController.connected
                            onClicked: diagnosticController.readStoredDtcs()
                        }

                        Button {
                            text: "Escanear Fallas Pendientes (Modo 07)"
                            enabled: diagnosticController.connected
                            onClicked: diagnosticController.readPendingDtcs()
                        }

                        // Módulos detectados dinámicamente en el JSON de OBDb
                        Repeater {
                            model: diagnosticController.availableModules
                            delegate: Button {
                                text: "Fallas " + modelData.type.toUpperCase() + " (" + modelData.header + ")"
                                enabled: diagnosticController.connected
                                onClicked: diagnosticController.scanModuleDtcs(modelData.header)
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "Borrar Códigos (Clear DTCs)"
                            enabled: diagnosticController.connected
                            background: Rectangle { color: "#b91c1c"; radius: 4 }
                            contentItem: Text {
                                text: parent.text
                                color: "#ffffff"
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: confirmClearDialog.open()
                        }
                    }

                    // Lista de Códigos Detectados
                    ListView {
                        id: dtcListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: diagnosticController.dtcList
                        spacing: 8

                        delegate: Rectangle {
                            width: dtcListView.width
                            height: 72
                            color: "#161f2e"
                            radius: 8
                            border.color: modelData.severity >= 3 ? "#ef4444" : (modelData.severity >= 2 ? "#f59e0b" : "#3b82f6")
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 14

                                Rectangle {
                                    width: 80
                                    height: 36
                                    radius: 4
                                    color: "#0f172a"
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.code
                                        color: "#f8fafc"
                                        font.bold: true
                                        font.pixelSize: 16
                                        font.family: "monospace"
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    RowLayout {
                                        spacing: 8
                                        Text {
                                            text: modelData.system
                                            color: "#94a3b8"
                                            font.bold: true
                                            font.pixelSize: 12
                                        }
                                        Text {
                                            visible: modelData.faultType && modelData.faultType.length > 0
                                            text: "Subtipo: 0x" + modelData.faultType
                                            color: "#a855f7"
                                            font.pixelSize: 11
                                            font.family: "monospace"
                                        }
                                    }

                                    Text {
                                        text: modelData.descriptionEs
                                        color: "#e2e8f0"
                                        font.pixelSize: 13
                                    }
                                }

                                Rectangle {
                                    width: 100
                                    height: 28
                                    radius: 14
                                    color: modelData.severity >= 3 ? "#7f1d1d" : (modelData.severity >= 2 ? "#78350f" : "#1e3a8a")

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.severity >= 3 ? "CRÍTICO" : (modelData.severity >= 2 ? "MODERADO" : "LEVE")
                                        color: "#ffffff"
                                        font.bold: true
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: dtcListView.count === 0
                            text: diagnosticController.connected ? "No se han detectado fallas activas o presione un botón de escaneo." : "Conecte el adaptador para diagnosticar fallas."
                            color: "#64748b"
                            font.pixelSize: 14
                        }
                    }
                }
            }

            // =================================================================
            // VISTA 4: CONSOLA / LOG DE TRAMAS CRUDAS (RAW TERMINAL LOG)
            // =================================================================
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    // Encabezado con título y botones de acción del log
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            text: "Monitor de Comunicación Cruda (Terminal OBD2 / UDS / ELM327)"
                            color: "#f8fafc"
                            font.bold: true
                            font.pixelSize: 14
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "Limpiar Log"
                            onClicked: diagnosticController.clearCommunicationLog()
                        }

                        Button {
                            text: "Exportar Log a TXT"
                            highlighted: true
                            onClicked: diagnosticController.exportCommunicationLog()
                        }
                    }

                    // Visor de Log estilo consola
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#090d16"
                        radius: 8
                        border.color: "#1e293b"
                        border.width: 1
                        clip: true

                        ListView {
                            id: logListView
                            anchors.fill: parent
                            anchors.margins: 10
                            model: diagnosticController.communicationLog
                            spacing: 4

                            onCountChanged: {
                                Qt.callLater(function() {
                                    logListView.positionViewAtEnd();
                                });
                            }

                            delegate: Text {
                                width: logListView.width - 20
                                wrapMode: Text.WrapAnywhere
                                text: modelData
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: {
                                    if (modelData.indexOf("TX ->") !== -1) return "#38bdf8";      // Cyan
                                    if (modelData.indexOf("RX <-") !== -1) return "#4ade80";      // Verde
                                    if (modelData.indexOf("[BLOQUEADO]") !== -1) return "#ef4444"; // Rojo
                                    if (modelData.indexOf("ERROR") !== -1) return "#f87171";      // Rojo suave
                                    if (modelData.indexOf("INFO") !== -1) return "#94a3b8";       // Gris
                                    return "#e2e8f0";
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: logListView.count === 0
                                text: "Consola de comunicación vacía. Envíe comandos o inicie la conexión."
                                color: "#475569"
                                font.family: "monospace"
                                font.pixelSize: 13
                            }
                        }
                    }

                    // Barra de Envío Manual de Comandos
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: rawCmdField
                            Layout.fillWidth: true
                            placeholderText: "Enviar comando AT / OBD2 / UDS (ej. AT DP, AT RV, 01 00, 21 01)..."
                            color: "#f8fafc"
                            font.family: "monospace"
                            onAccepted: {
                                if (rawCmdField.text.trim().length > 0) {
                                    diagnosticController.sendDiagnosticCommand(rawCmdField.text.trim());
                                    rawCmdField.clear();
                                }
                            }
                        }

                        Button {
                            text: "Enviar"
                            highlighted: true
                            enabled: diagnosticController.connected && rawCmdField.text.trim().length > 0
                            onClicked: {
                                diagnosticController.sendDiagnosticCommand(rawCmdField.text.trim());
                                rawCmdField.clear();
                            }
                        }
                    }

                    // Botones de Acceso Rápido (Pills de comandos seguros)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "Comandos Rápidos:"
                            color: "#64748b"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Repeater {
                            model: ["AT DP", "AT RV", "01 00", "01 0C", "09 02"]
                            delegate: Button {
                                text: modelData
                                Layout.preferredHeight: 28
                                font.family: "monospace"
                                font.pixelSize: 11
                                enabled: diagnosticController.connected
                                onClicked: diagnosticController.sendDiagnosticCommand(modelData)
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    // Cuadro de Diálogo de Confirmación Segura de Borrado (SafetyGuard & Triple Candado KOEO)
    Dialog {
        id: confirmClearDialog
        title: "Confirmar Borrado de Códigos de Falla (DTC)"
        modal: true
        anchors.centerIn: parent
        width: 480
        standardButtons: Dialog.Yes | Dialog.No

        ColumnLayout {
            spacing: 12
            width: parent.width

            Text {
                text: "TRIPLE CANDADO DE SEGURIDAD KOEO (Modo 04 / UDS 0x14)"
                color: "#ef4444"
                font.bold: true
                font.pixelSize: 14
            }

            Text {
                text: "El borrado de DTCs restablecerá monitores de emisiones (Readiness Monitors) y congelados de pantalla (Freeze Frame).\n\n" +
                      "Candados de Hardware obligatorios antes de enviar 04:\n" +
                      "• 1. Motor apagado (RPM ≤ 50)\n" +
                      "• 2. Vehículo detenido (0 km/h)\n" +
                      "• 3. Batería estable (≥ 11.5 V)\n\n" +
                      "¿Desea proceder con el borrado en la ECU?"
                color: "#e2e8f0"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                font.pixelSize: 13
            }
        }

        onAccepted: {
            diagnosticController.clearDtcs(true);
        }
        onRejected: {
            diagnosticController.clearDtcs(false);
        }
    }

    // Diálogo de Alerta de Estado del Sistema (Resultados de Modo 04 / Errores)
    Dialog {
        id: statusAlertDialog
        title: "Aviso del Sistema"
        modal: true
        anchors.centerIn: parent
        width: 460
        standardButtons: Dialog.Ok

        property alias alertTitle: statusAlertDialog.title
        property string alertMessage: ""
        property bool isError: false

        ColumnLayout {
            spacing: 12
            width: parent.width

            Text {
                text: statusAlertDialog.isError ? "ATENCIÓN / RESTRICCIÓN DE SEGURIDAD" : "OPERACIÓN COMPLETADA"
                color: statusAlertDialog.isError ? "#ef4444" : "#10b981"
                font.bold: true
                font.pixelSize: 13
            }

            Text {
                text: statusAlertDialog.alertMessage
                color: "#f8fafc"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                font.pixelSize: 13
            }
        }
    }

    Connections {
        target: diagnosticController
        function onStatusAlert(title, message, isError) {
            statusAlertDialog.alertTitle = title;
            statusAlertDialog.alertMessage = message;
            statusAlertDialog.isError = isError;
            statusAlertDialog.open();
        }
    }
}
