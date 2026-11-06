import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * LiveGraphView.qml
 * Visualizador de osciloscopio y gráficas en tiempo real de telemetría vehicular.
 * Cumplimiento estricto LGPLv3: Renderizado nativo 100% sobre QtQuick Canvas (SIN QtCharts / QtGraphs).
 */
Rectangle {
    id: root
    color: "#0f141c"

    property string channelName: "RPM del Motor"
    property string unit: "RPM"
    property real currentValue: 0.0
    property real minValue: 0.0
    property real maxValue: 6000.0
    property color lineColor: "#00e5ff"
    property color glowColor: "#3300e5ff"
    property var dataPoints: []
    property int maxSamples: 120
    property bool isPaused: false

    // Actualizar historial cuando cambia el valor actual
    onCurrentValueChanged: {
        if (isPaused) return;
        var points = dataPoints.slice();
        points.push(currentValue);
        if (points.length > maxSamples) {
            points.shift();
        }
        dataPoints = points;
        canvas.requestPaint();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // Barra superior de estadísticas y selector de canal
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                width: 12
                height: 12
                radius: 6
                color: root.lineColor
            }

            Text {
                text: root.channelName + " (" + root.unit + ")"
                color: "#e0e6ed"
                font.bold: true
                font.pixelSize: 15
            }

            Item { Layout.fillWidth: true }

            Text {
                text: "Valor Actual: " + root.currentValue.toFixed(1) + " " + root.unit
                color: root.lineColor
                font.bold: true
                font.pixelSize: 16
                font.family: "monospace"
            }

            Button {
                text: root.isPaused ? "Reanudar" : "Pausar"
                onClicked: root.isPaused = !root.isPaused
            }

            Button {
                text: "Limpiar"
                onClicked: {
                    root.dataPoints = [];
                    canvas.requestPaint();
                }
            }
        }

        // Lienzo de dibujo gráfico en tiempo real
        Canvas {
            id: canvas
            Layout.fillWidth: true
            Layout.fillHeight: true

            onPaint: {
                var ctx = getContext("2d");
                var w = width;
                var h = height;

                ctx.save();
                ctx.clearRect(0, 0, w, h);

                // 1. Fondo del Osciloscopio
                ctx.fillStyle = "#141b24";
                ctx.fillRect(0, 0, w, h);

                // 2. Rejilla de Referencia (Grid)
                ctx.strokeStyle = "#1f2a38";
                ctx.lineWidth = 1;

                var vDivs = 10;
                for (var i = 1; i < vDivs; ++i) {
                    var x = (w / vDivs) * i;
                    ctx.beginPath();
                    ctx.moveTo(x, 0);
                    ctx.lineTo(x, h);
                    ctx.stroke();
                }

                var hDivs = 6;
                for (var j = 1; j < hDivs; ++j) {
                    var y = (h / hDivs) * j;
                    ctx.beginPath();
                    ctx.moveTo(0, y);
                    ctx.lineTo(w, y);
                    ctx.stroke();

                    // Etiquetas de escala Y
                    var val = root.maxValue - ((root.maxValue - root.minValue) / hDivs) * j;
                    ctx.fillStyle = "#607286";
                    ctx.font = "10px monospace";
                    ctx.fillText(val.toFixed(0), 6, y - 4);
                }

                // 3. Trazo de la Curva de Telemetría
                if (root.dataPoints.length > 1) {
                    var pts = root.dataPoints;
                    var stepX = w / (root.maxSamples - 1);
                    var rangeY = (root.maxValue - root.minValue);
                    if (rangeY <= 0) rangeY = 1.0;

                    // Área translúcida bajo la curva
                    ctx.beginPath();
                    var startX = 0;
                    var startY = h - ((pts[0] - root.minValue) / rangeY) * h;
                    ctx.moveTo(startX, h);
                    ctx.lineTo(startX, startY);

                    for (var k = 1; k < pts.length; ++k) {
                        var px = k * stepX;
                        var py = h - ((pts[k] - root.minValue) / rangeY) * h;
                        py = Math.max(0, Math.min(h, py));
                        ctx.lineTo(px, py);
                    }
                    ctx.lineTo((pts.length - 1) * stepX, h);
                    ctx.closePath();
                    ctx.fillStyle = root.glowColor;
                    ctx.fill();

                    // Línea principal brillante
                    ctx.beginPath();
                    ctx.strokeStyle = root.lineColor;
                    ctx.lineWidth = 2.5;
                    ctx.lineJoin = "round";

                    for (var m = 0; m < pts.length; ++m) {
                        var lx = m * stepX;
                        var ly = h - ((pts[m] - root.minValue) / rangeY) * h;
                        ly = Math.max(0, Math.min(h, ly));
                        if (m === 0) {
                            ctx.moveTo(lx, ly);
                        } else {
                            ctx.lineTo(lx, ly);
                        }
                    }
                    ctx.stroke();

                    // Punto activo final
                    var lastX = (pts.length - 1) * stepX;
                    var lastY = h - ((pts[pts.length - 1] - root.minValue) / rangeY) * h;
                    lastY = Math.max(0, Math.min(h, lastY));

                    ctx.beginPath();
                    ctx.fillStyle = "#ffffff";
                    ctx.arc(lastX, lastY, 4, 0, 2 * Math.PI);
                    ctx.fill();
                }

                // Marco exterior
                ctx.strokeStyle = "#273648";
                ctx.lineWidth = 1.5;
                ctx.strokeRect(0, 0, w, h);

                ctx.restore();
            }
        }
    }
}

