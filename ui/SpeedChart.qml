import QtQuick
Canvas {
    id: chart
    property var samples: []
    onSamplesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d"); c.reset()
        const left = 27, top = 6, bottom = height - 20, right = width - 8, w = right-left, h = bottom-top
        if (w <= 0 || h <= 0) return
        let peak = 0
        for (const s of samples) peak = Math.max(peak, s.down, s.up)
        const maximum = Math.max(600, Math.ceil(peak / 600) * 600)
        const duration = Math.max(20, samples.length && samples[samples.length-1].seconds ? Math.ceil(samples[samples.length-1].seconds / 5) * 5 : 20)
        c.font = "9px 'Segoe UI'"; c.fillStyle = "#7483a5"; c.lineWidth = 0.7
        for (let n = 0; n <= 3; n++) {
            const y = bottom - h * n / 3
            c.setLineDash([2,2]); c.strokeStyle = "#dce6f3"; c.beginPath(); c.moveTo(left,y); c.lineTo(right,y); c.stroke()
            c.textAlign = "right"; c.textBaseline = "middle"; c.fillText(String(maximum * n / 3), left-9,y)
        }
        for (let n = 0; n <= 4; n++) {
            const x = left + w * n / 4
            c.setLineDash([2,2]); c.beginPath(); c.moveTo(x,top); c.lineTo(x,bottom); c.stroke()
            c.textAlign = "center"; c.textBaseline = "top"; c.fillText(String(Math.round(duration * n / 4)) + "s",x,bottom+10)
        }
        c.setLineDash([]); c.strokeStyle = "#b7c7df"; c.beginPath(); c.moveTo(left,top); c.lineTo(left,bottom); c.lineTo(right,bottom); c.stroke()
        function draw(key,color,fill) {
            if (!samples.length) return
            c.beginPath()
            let first=left,last=left
            for (let i=0; i<samples.length; ++i) {
                const seconds = samples[i].seconds === undefined ? i * duration / Math.max(1,samples.length-1) : samples[i].seconds
                const x = left + Math.min(1,seconds/duration)*w, y = bottom - samples[i][key]/maximum*h
                if (!i) {first=x; c.moveTo(x,y)} else c.lineTo(x,y)
                last=x
            }
            c.strokeStyle=color; c.lineWidth=1.5; c.lineJoin="round"; c.stroke()
            if (fill) {c.lineTo(last,bottom); c.lineTo(first,bottom); c.closePath(); const g=c.createLinearGradient(0,top,0,bottom); g.addColorStop(0,"#dbe8ff"); g.addColorStop(1,"#f5f9ff"); c.fillStyle=g; c.fill()}
        }
        draw("down","#3475ff",true); draw("down","#3475ff",false); draw("up","#009d98",false)
    }
}
