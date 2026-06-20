import QtQuick

Item {
    id: staticRoot

    property bool running: visible
    property int frameCount: 12
    property int frameWidth: 96
    property int frameHeight: 72
    property int frameInterval: 83
    property int frameIndex: 0

    Rectangle {
        anchors.fill: parent
        color: "#030303"
    }

    Item {
        anchors.fill: parent
        clip: true

        Image {
            id: coarseNoise
            anchors.fill: parent
            source: Qt.resolvedUrl("../../assets/images/static-noise-strip.png")
            sourceClipRect: Qt.rect(staticRoot.frameIndex * staticRoot.frameWidth,
                                    0,
                                    staticRoot.frameWidth,
                                    staticRoot.frameHeight)
            fillMode: Image.Tile
            smooth: false
            mipmap: false
            cache: true
            opacity: 0.82
        }

        Image {
            x: -staticRoot.frameWidth / 2
            y: -staticRoot.frameHeight / 3
            width: parent.width + staticRoot.frameWidth
            height: parent.height + staticRoot.frameHeight
            source: coarseNoise.source
            sourceClipRect: Qt.rect(((staticRoot.frameIndex + 5) % staticRoot.frameCount) * staticRoot.frameWidth,
                                    0,
                                    staticRoot.frameWidth,
                                    staticRoot.frameHeight)
            fillMode: Image.Tile
            smooth: false
            mipmap: false
            cache: true
            opacity: 0.20
        }
    }

    Image {
        anchors.fill: parent
        source: Qt.resolvedUrl("../../assets/images/static-scanlines.png")
        fillMode: Image.Tile
        smooth: false
        mipmap: false
        cache: true
        opacity: 0.68
    }

    Rectangle {
        id: trackingBand
        width: parent.width
        height: Math.max(3, parent.height * 0.018)
        y: -height
        color: "#F8F8F8"
        opacity: 0.12
        visible: staticRoot.running

        SequentialAnimation on y {
            running: staticRoot.running
            loops: Animation.Infinite
            PauseAnimation { duration: 850 }
            NumberAnimation {
                from: -trackingBand.height
                to: staticRoot.height
                duration: 980
                easing.type: Easing.Linear
            }
            PauseAnimation { duration: 1750 }
            NumberAnimation {
                from: staticRoot.height * 0.34
                to: staticRoot.height * 0.42
                duration: 115
                easing.type: Easing.Linear
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.16
    }

    Timer {
        interval: staticRoot.frameInterval
        repeat: true
        running: staticRoot.running && staticRoot.visible && staticRoot.width > 0 && staticRoot.height > 0
        onTriggered: staticRoot.frameIndex = (staticRoot.frameIndex + 1) % staticRoot.frameCount
    }
}
