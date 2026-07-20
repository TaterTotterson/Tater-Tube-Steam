pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: staticRoot

    property bool running: visible
    property string themeName: "Off Air"
    property int frameCount: 12
    property int frameWidth: 96
    property int frameHeight: 72
    property int frameInterval: 83
    property int frameIndex: 0

    function testBarColor(index) {
        var colors = ["#D9D9D9", "#D9D629", "#29D7D7", "#27D12F",
                      "#D52DCE", "#D62C32", "#2D38D2"]
        return colors[index % colors.length]
    }

    function saturdayColor(index) {
        var colors = ["#FF4F8B", "#FFD53D", "#4DE8FF", "#FF7A1A", "#9E72FF"]
        return colors[index % colors.length]
    }

    function activeBackground() {
        if (themeName === "TaterVision '87") return taterVisionBackground
        if (themeName === "Broadcast Test") return broadcastTestBackground
        if (themeName === "Cable After Midnight") return midnightCableBackground
        if (themeName === "Public Access") return publicAccessBackground
        if (themeName === "Woodgrain Console") return woodgrainBackground
        if (themeName === "Tater Satellite") return satelliteBackground
        if (themeName === "Haunted Tape") return hauntedTapeBackground
        if (themeName === "Saturday Morning") return saturdayMorningBackground
        return offAirBackground
    }

    Loader {
        anchors.fill: parent
        sourceComponent: staticRoot.activeBackground()
    }

    Component {
        id: offAirBackground

        Item {
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
                    sourceClipRect: Qt.rect(
                        ((staticRoot.frameIndex + 5) % staticRoot.frameCount)
                            * staticRoot.frameWidth,
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
                anchors.fill: parent
                color: "#000000"
                opacity: 0.16
            }
        }
    }

    Component {
        id: taterVisionBackground

        Item {
            id: taterVision
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#3B1E0E" }
                    GradientStop { position: 0.48; color: "#1F0F08" }
                    GradientStop { position: 1.0; color: "#0E0806" }
                }
            }

            Repeater {
                model: 18
                Rectangle {
                    required property int index
                    x: 0
                    y: (index + 0.35) * taterVision.height / 18
                    width: taterVision.width
                    height: Math.max(1, taterVision.height * (index % 4 === 0 ? 0.004 : 0.002))
                    color: index % 3 === 0 ? "#FF9B3E" : "#B56B35"
                    opacity: index % 3 === 0 ? 0.10 : 0.055
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: Math.min(parent.width, parent.height) * 0.055
                radius: Math.min(parent.width, parent.height) * 0.055
                color: "transparent"
                border.color: "#F4B56B"
                border.width: Math.max(1, parent.height * 0.004)
                opacity: 0.18
            }

            Rectangle {
                width: parent.width * 0.72
                height: parent.height * 0.11
                x: parent.width * 0.14
                y: parent.height * 0.76
                radius: height / 2
                color: "#FF7518"
                opacity: 0.075
            }
        }
    }

    Component {
        id: broadcastTestBackground

        Item {
            id: broadcastTest
            clip: true

            Rectangle {
                anchors.fill: parent
                color: "#090D12"
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: parent.height * 0.61

                Repeater {
                    model: 7
                    Rectangle {
                        required property int index
                        width: broadcastTest.width / 7
                        height: parent.height
                        color: staticRoot.testBarColor(index)
                        opacity: 0.30
                    }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.height * 0.25

                Repeater {
                    model: 7
                    Rectangle {
                        required property int index
                        width: broadcastTest.width / 7
                        height: parent.height
                        color: staticRoot.testBarColor(6 - index)
                        opacity: index % 2 === 0 ? 0.15 : 0.05
                    }
                }
            }

            Repeater {
                model: 9
                Rectangle {
                    required property int index
                    x: index * broadcastTest.width / 8
                    width: Math.max(1, broadcastTest.width * 0.0015)
                    height: broadcastTest.height
                    color: "#FFFFFF"
                    opacity: 0.09
                }
            }

            Repeater {
                model: 7
                Rectangle {
                    required property int index
                    y: index * broadcastTest.height / 6
                    width: broadcastTest.width
                    height: Math.max(1, broadcastTest.height * 0.0025)
                    color: "#FFFFFF"
                    opacity: 0.08
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#02050A"
                opacity: 0.48
            }
        }
    }

    Component {
        id: midnightCableBackground

        Item {
            id: midnightCable
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#120B35" }
                    GradientStop { position: 0.48; color: "#071329" }
                    GradientStop { position: 1.0; color: "#02040E" }
                }
            }

            Rectangle {
                x: 0
                y: parent.height * 0.48
                width: parent.width
                height: Math.max(1, parent.height * 0.006)
                color: "#FF4EDB"
                opacity: 0.24
            }

            Repeater {
                model: 11
                Rectangle {
                    required property int index
                    x: 0
                    y: midnightCable.height * 0.51
                       + Math.pow(index / 10.0, 1.55) * midnightCable.height * 0.49
                    width: midnightCable.width
                    height: Math.max(1, midnightCable.height * 0.0025)
                    color: index % 2 === 0 ? "#42D9FF" : "#B74DFF"
                    opacity: 0.16
                }
            }

            Repeater {
                model: 13
                Rectangle {
                    required property int index
                    x: index * midnightCable.width / 12
                    y: midnightCable.height * 0.49
                    width: Math.max(1, midnightCable.width * 0.0015)
                    height: midnightCable.height * 0.62
                    color: "#42D9FF"
                    opacity: 0.10
                    transformOrigin: Item.Top
                    rotation: (index - 6) * 4.6
                }
            }

            Rectangle {
                id: cableGlitch
                x: -width
                y: parent.height * 0.31
                width: parent.width * 0.34
                height: Math.max(2, parent.height * 0.012)
                color: "#C65CFF"
                opacity: 0.18

                SequentialAnimation on x {
                    running: staticRoot.running
                    loops: Animation.Infinite
                    PauseAnimation { duration: 1800 }
                    NumberAnimation {
                        from: -cableGlitch.width
                        to: midnightCable.width
                        duration: 520
                        easing.type: Easing.Linear
                    }
                    PauseAnimation { duration: 2500 }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#00020B"
                opacity: 0.22
            }
        }
    }

    Component {
        id: publicAccessBackground

        Item {
            id: publicAccess
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#211875" }
                    GradientStop { position: 0.55; color: "#10105A" }
                    GradientStop { position: 1.0; color: "#080A31" }
                }
            }

            Rectangle {
                width: parent.width * 0.72
                height: parent.height * 0.15
                x: -parent.width * 0.14
                y: parent.height * 0.12
                rotation: -12
                color: "#FF4FA3"
                opacity: 0.18
            }

            Rectangle {
                width: parent.width * 0.62
                height: parent.height * 0.12
                x: parent.width * 0.55
                y: parent.height * 0.67
                rotation: -18
                color: "#4DE8FF"
                opacity: 0.18
            }

            Rectangle {
                width: parent.width * 0.32
                height: width
                x: parent.width * 0.76
                y: -height * 0.42
                radius: width / 2
                color: "#FFD53D"
                opacity: 0.13
            }

            Repeater {
                model: 30
                Rectangle {
                    required property int index
                    width: Math.max(2, publicAccess.height * 0.009)
                    height: width
                    radius: width / 2
                    x: ((index * 83) % 997) / 997 * publicAccess.width
                    y: ((index * 47 + 91) % 613) / 613 * publicAccess.height
                    color: index % 3 === 0 ? "#FFD53D"
                          : (index % 3 === 1 ? "#4DE8FF" : "#FF4FA3")
                    opacity: 0.12
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#07082D"
                opacity: 0.30
            }
        }
    }

    Component {
        id: woodgrainBackground

        Item {
            id: woodgrain
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#4A2512" }
                    GradientStop { position: 0.44; color: "#251107" }
                    GradientStop { position: 1.0; color: "#120805" }
                }
            }

            Repeater {
                model: 34
                Rectangle {
                    required property int index
                    x: index * woodgrain.width / 33
                    width: Math.max(1, woodgrain.width * (index % 5 === 0 ? 0.004 : 0.002))
                    height: woodgrain.height
                    color: index % 4 === 0 ? "#D08B4D" : "#6F351A"
                    opacity: index % 4 === 0 ? 0.11 : 0.075
                }
            }

            Rectangle {
                width: parent.width * 0.23
                height: parent.height * 0.055
                x: parent.width * 0.07
                y: parent.height * 0.20
                radius: height / 2
                color: "transparent"
                border.color: "#B36B32"
                border.width: Math.max(1, parent.height * 0.004)
                opacity: 0.14
                rotation: -4
            }

            Rectangle {
                width: parent.width * 0.18
                height: parent.height * 0.045
                x: parent.width * 0.70
                y: parent.height * 0.72
                radius: height / 2
                color: "transparent"
                border.color: "#D28A45"
                border.width: Math.max(1, parent.height * 0.003)
                opacity: 0.12
                rotation: 5
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: Math.min(parent.width, parent.height) * 0.035
                radius: Math.min(parent.width, parent.height) * 0.025
                color: "transparent"
                border.color: "#E2A55F"
                border.width: Math.max(1, parent.height * 0.004)
                opacity: 0.13
            }

            Rectangle {
                anchors.fill: parent
                color: "#0A0402"
                opacity: 0.34
            }
        }
    }

    Component {
        id: satelliteBackground

        Item {
            id: satellite
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#071E28" }
                    GradientStop { position: 0.48; color: "#031018" }
                    GradientStop { position: 1.0; color: "#010508" }
                }
            }

            Repeater {
                model: 48
                Rectangle {
                    required property int index
                    width: Math.max(1, satellite.height * (index % 7 === 0 ? 0.006 : 0.003))
                    height: width
                    radius: width / 2
                    x: ((index * 89 + 17) % 997) / 997 * satellite.width
                    y: ((index * 53 + 31) % 613) / 613 * satellite.height
                    color: index % 9 === 0 ? "#FF9A52" : "#D9FFF5"
                    opacity: index % 7 === 0 ? 0.42 : 0.22
                }
            }

            Item {
                id: radar
                width: Math.min(parent.width, parent.height) * 0.72
                height: width
                x: parent.width - width * 0.76
                y: parent.height * 0.50 - height / 2

                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        anchors.centerIn: parent
                        width: radar.width * (index + 1) / 4
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.color: "#6DDBB5"
                        border.width: Math.max(1, radar.width * 0.004)
                        opacity: 0.11
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: Math.max(1, parent.height * 0.005)
                    color: "#6DDBB5"
                    opacity: 0.13
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.max(1, parent.width * 0.005)
                    height: parent.height
                    color: "#6DDBB5"
                    opacity: 0.13
                }

                Rectangle {
                    id: radarSweep
                    anchors.left: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width / 2
                    height: Math.max(2, parent.height * 0.009)
                    transformOrigin: Item.Left
                    color: "#FF7A1A"
                    opacity: 0.23

                    RotationAnimation on rotation {
                        running: staticRoot.running
                        from: 0
                        to: 360
                        duration: 7600
                        loops: Animation.Infinite
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#010609"
                opacity: 0.24
            }
        }
    }

    Component {
        id: hauntedTapeBackground

        Item {
            id: hauntedTape
            clip: true

            Rectangle {
                anchors.fill: parent
                color: "#07100A"
            }

            Image {
                anchors.fill: parent
                source: Qt.resolvedUrl("../../assets/images/static-noise-strip.png")
                sourceClipRect: Qt.rect(
                    ((staticRoot.frameIndex + 8) % staticRoot.frameCount)
                        * staticRoot.frameWidth,
                    0,
                    staticRoot.frameWidth,
                    staticRoot.frameHeight)
                fillMode: Image.Tile
                smooth: false
                mipmap: false
                cache: true
                opacity: 0.26
            }

            Rectangle {
                anchors.fill: parent
                color: "#183A21"
                opacity: 0.42
            }

            Repeater {
                model: 13
                Rectangle {
                    required property int index
                    x: 0
                    y: index * hauntedTape.height / 12
                    width: hauntedTape.width
                    height: Math.max(1, hauntedTape.height * (index % 4 === 0 ? 0.006 : 0.002))
                    color: index % 4 === 0 ? "#C7F5B8" : "#5E8660"
                    opacity: index % 4 === 0 ? 0.11 : 0.065
                }
            }

            Rectangle {
                id: ghostBand
                width: parent.width
                height: parent.height * 0.12
                y: -height
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#003E5B3F" }
                    GradientStop { position: 0.5; color: "#557EC66F" }
                    GradientStop { position: 1.0; color: "#003E5B3F" }
                }
                opacity: 0.28

                SequentialAnimation on y {
                    running: staticRoot.running
                    loops: Animation.Infinite
                    PauseAnimation { duration: 1100 }
                    NumberAnimation {
                        from: -ghostBand.height
                        to: hauntedTape.height
                        duration: 2100
                        easing.type: Easing.Linear
                    }
                    PauseAnimation { duration: 1800 }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#000703"
                opacity: 0.28
            }
        }
    }

    Component {
        id: saturdayMorningBackground

        Item {
            id: saturdayMorning
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#2435A0" }
                    GradientStop { position: 0.56; color: "#131B68" }
                    GradientStop { position: 1.0; color: "#090D38" }
                }
            }

            Repeater {
                model: 5
                Rectangle {
                    required property int index
                    width: saturdayMorning.width * (0.18 + (index % 2) * 0.06)
                    height: width
                    radius: width / 2
                    x: ((index * 0.23 + 0.04) % 0.92) * saturdayMorning.width
                    y: ((index * 0.31 + 0.08) % 0.86) * saturdayMorning.height
                    color: staticRoot.saturdayColor(index)
                    opacity: 0.14
                }
            }

            Repeater {
                model: 9
                Rectangle {
                    required property int index
                    width: saturdayMorning.width * 0.28
                    height: Math.max(5, saturdayMorning.height * 0.025)
                    x: -saturdayMorning.width * 0.08 + index * saturdayMorning.width * 0.15
                    y: saturdayMorning.height * 0.11 + (index % 4) * saturdayMorning.height * 0.24
                    rotation: -18
                    radius: height / 2
                    color: staticRoot.saturdayColor(index + 2)
                    opacity: 0.09
                }
            }

            Repeater {
                model: 36
                Rectangle {
                    required property int index
                    width: Math.max(2, saturdayMorning.height * 0.008)
                    height: width
                    radius: width / 2
                    x: ((index * 71 + 23) % 997) / 997 * saturdayMorning.width
                    y: ((index * 41 + 67) % 613) / 613 * saturdayMorning.height
                    color: staticRoot.saturdayColor(index)
                    opacity: 0.12
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#080C36"
                opacity: 0.32
            }
        }
    }

    Image {
        anchors.fill: parent
        source: Qt.resolvedUrl("../../assets/images/static-scanlines.png")
        fillMode: Image.Tile
        smooth: false
        mipmap: false
        cache: true
        visible: staticRoot.themeName !== "Off Air"
        opacity: staticRoot.themeName === "Haunted Tape" ? 0.58 : 0.34
    }

    Rectangle {
        id: trackingBand
        width: parent.width
        height: Math.max(3, parent.height * 0.018)
        y: -height
        color: staticRoot.themeName === "Haunted Tape" ? "#B8FF9A"
             : (staticRoot.themeName === "Cable After Midnight" ? "#A955FF" : "#F8F8F8")
        opacity: staticRoot.themeName === "Off Air" ? 0.12 : 0.07
        visible: staticRoot.running
                 && (staticRoot.themeName === "Off Air"
                     || staticRoot.themeName === "Haunted Tape"
                     || staticRoot.themeName === "Cable After Midnight")

        SequentialAnimation on y {
            running: trackingBand.visible
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

    Timer {
        interval: staticRoot.frameInterval
        repeat: true
        running: staticRoot.running
                 && staticRoot.visible
                 && staticRoot.width > 0
                 && staticRoot.height > 0
                 && (staticRoot.themeName === "Off Air"
                     || staticRoot.themeName === "Haunted Tape")
        onTriggered: staticRoot.frameIndex = (staticRoot.frameIndex + 1) % staticRoot.frameCount
    }
}
