import QtQuick 2.15

// Mini bar-graph visualizer for incoming MIDI notes
Item {
    id: root
    property color barColor: "#7c3aed"

    // Ring buffer of last N notes
    property var bars: []
    readonly property int maxBars: 24

    function flash(note, velocity) {
        var newBars = bars.slice()
        if (newBars.length >= maxBars) newBars.shift()
        newBars.push({ note: note, vel: velocity, age: 0 })
        bars = newBars
        canvas.requestPaint()
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var bars = root.bars
            if (bars.length === 0) return

            var barW = (width - (bars.length - 1) * 3) / Math.max(bars.length, 1)
            for (var i = 0; i < bars.length; i++) {
                var b = bars[i]
                var h = Math.max(4, (b.vel / 127.0) * height)
                var x = i * (barW + 3)
                var y = height - h
                var alpha = 0.3 + 0.7 * (i / bars.length)
                ctx.fillStyle = "rgba(124,58,237," + alpha + ")"
                ctx.beginPath()
                ctx.roundRect(x, y, Math.max(barW, 2), h, 3)
                ctx.fill()
            }
        }
    }

    Timer {
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            if (root.bars.length > 0) canvas.requestPaint()
        }
    }
}
