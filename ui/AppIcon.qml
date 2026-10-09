import QtQuick
import QtQuick.Shapes

Item {
    id: icon
    property string kind: "home"
    property color ink: "#17203b"
    implicitWidth: 24
    implicitHeight: 24
    readonly property string outline: {
        switch (kind) {
        case "home": return "M3 10.5 L12 3 L21 10.5 V20 A1 1 0 0 1 20 21 H15 V14 H9 V21 H4 A1 1 0 0 1 3 20 Z"
        case "network": case "globe": return "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 Z M16 12 A4 9 0 1 1 8 12 A4 9 0 1 1 16 12 Z M3 12 H21"
        case "log": return "M7 3 H17 A2 2 0 0 1 19 5 V19 A2 2 0 0 1 17 21 H7 A2 2 0 0 1 5 19 V5 A2 2 0 0 1 7 3 Z M8 8 H16 M8 12 H16 M8 16 H13"
        case "settings": return "M10 2 H14 L14.7 4.6 L17 6 L19.6 5.4 L21.6 8.8 L19.7 10.7 V13.3 L21.6 15.2 L19.6 18.6 L17 18 L14.7 19.4 L14 22 H10 L9.3 19.4 L7 18 L4.4 18.6 L2.4 15.2 L4.3 13.3 V10.7 L2.4 8.8 L4.4 5.4 L7 6 L9.3 4.6 Z M15.5 12 A3.5 3.5 0 1 1 8.5 12 A3.5 3.5 0 1 1 15.5 12 Z"
        case "user": return "M16 7 A4 4 0 1 1 8 7 A4 4 0 1 1 16 7 Z M4 21 V19 A8 8 0 0 1 20 19 V21"
        case "path": return "M9 2 H15 V7 H9 Z M2 17 H8 V22 H2 Z M16 17 H22 V22 H16 Z M12 7 V12 M5 17 V12 H19 V17"
        case "clock": return "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 Z M12 6 V12 L16 14"
        case "heartbeat": return "M2 13 H6 L9 5 L13 20 L16 9 L19 13 H22"
        case "check": return "M5 12 L10 17 L19 7"
        case "cross": return "M6 6 L18 18 M18 6 L6 18"
        case "shield": return "M12 2 L21 6 L20 14 Q19 18 12 22 Q5 18 4 14 L3 6 Z M8 11 L11 14 L16 8"
        case "eye": return "M2 12 Q12 1 22 12 Q12 23 2 12 Z M15 12 A3 3 0 1 1 9 12 A3 3 0 1 1 15 12 Z"
        case "power": return "M6.3 6.3 A8 8 0 1 0 17.7 6.3 M12 2 V12"
        case "download": return "M12 3 V16 M7 11 L12 16 L17 11 M4 17 V21 H20 V17"
        case "upload": return "M12 16 V3 M7 8 L12 3 L17 8 M4 17 V21 H20 V17"
        case "downloadArrow": return "M12 3 V21 M4 13 L12 21 L20 13"
        case "uploadArrow": return "M12 21 V3 M4 11 L12 3 L20 11"
        case "traffic": return "M3 18 L8 11 L13 14 L20 5 M15 5 H20 V10"
        case "refresh": return "M20 8 A9 9 0 1 0 20 17 M20 3 V8 H15"
        case "speed": return "M4.2 18.5 A9.3 9.3 0 1 1 19.8 18.5"
        case "copy": return "M9 7 H19 A2 2 0 0 1 21 9 V19 A2 2 0 0 1 19 21 H9 A2 2 0 0 1 7 19 V9 A2 2 0 0 1 9 7 M17 7 V5 A2 2 0 0 0 15 3 H5 A2 2 0 0 0 3 5 V15 A2 2 0 0 0 5 17 H7"
        default: return ""
        }
    }
    Item {
        width: 24; height: 24; anchors.centerIn: parent
        scale: Math.min(icon.width, icon.height) / 24
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: icon.ink; strokeWidth: icon.kind === "speed" ? 2.2 : icon.kind === "check" || icon.kind === "cross" ? 2.2 : 1.65
                fillColor: "transparent"; capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                PathSvg { path: icon.outline }
            }
            ShapePath {
                strokeColor: icon.kind === "speed" ? icon.ink : "transparent"; strokeWidth: 0.8
                fillColor: "transparent"; capStyle: ShapePath.RoundCap
                PathSvg { path: icon.kind === "speed" ? "M5.4 9.2 L6.6 9.8 M10.4 3.7 L10.7 5 M17.7 6 L16.7 7" : "" }
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: icon.kind === "speed" ? icon.ink : "transparent"
                fillRule: ShapePath.WindingFill
                PathSvg { path: icon.kind === "speed" ? "M10.4 11.3 L17.1 6.9 L13.6 13.7 Z M14.3 12.8 A2.5 2.5 0 1 1 9.3 12.8 A2.5 2.5 0 1 1 14.3 12.8 Z" : "" }
            }
        }
    }
}
