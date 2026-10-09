import QtQuick

Canvas {
    id: chart
    property var samples: []
    onSamplesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.lineWidth = 1
        c.strokeStyle = "#e8edf5"
        for (let n = 1; n < 4; n++) {
            const y = height * n / 4
            c.beginPath(); c.moveTo(0, y); c.lineTo(width, y); c.stroke()
        }
        let maximum = 1024
        for (let i = 0; i < samples.length; i++) maximum = Math.max(maximum, samples[i].down, samples[i].up)
        const dx = width / 44
        function trace(key, color) {
            c.beginPath()
            let drawing = false
            for (let i = 0; i < samples.length; i++) {
                const p = samples[i]
                if (!p.valid) { drawing = false; continue }
                const x = (45 - samples.length + i) * dx
                const y = height - 4 - p[key] / maximum * (height - 10)
                if (drawing) c.lineTo(x, y); else c.moveTo(x, y)
                drawing = true
            }
            c.strokeStyle = color; c.lineWidth = 2; c.lineJoin = "round"; c.stroke()
        }
        trace("down", "#326bdc"); trace("up", "#56b9be")
    }
}
