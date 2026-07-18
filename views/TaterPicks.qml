pragma ComponentBehavior: Bound

import QtQuick
import Components

FocusScope {
    id: picksRoot

    signal navigateTo(string path, var params, var listState)
    signal goBack()

    property var navParams: ({})
    property var picks: []

    focus: true

    function reloadPicks() {
        picks = (appCore.taterRecommendations || []).slice()
        if (pickList.count > 0) {
            pickList.currentIndex = Math.max(0, Math.min(pickList.currentIndex, pickList.count - 1))
            pickList.positionViewAtIndex(pickList.currentIndex, ListView.Contain)
        }
    }

    function selectedPick() {
        if (pickList.currentIndex < 0 || pickList.currentIndex >= picks.length)
            return ({})
        return picks[pickList.currentIndex] || ({})
    }

    function playSelected() {
        var recommendation = selectedPick()
        if (!recommendation.id)
            return
        appCore.sendTaterRecommendationFeedback(recommendation.id, "played")
        navigateTo("modules/usenet/views/Root.qml",
                   { recommendation: recommendation },
                   { currentIndex: pickList.currentIndex })
    }

    function dismissSelected() {
        var recommendation = selectedPick()
        if (!recommendation.id)
            return
        appCore.sendTaterRecommendationFeedback(recommendation.id, "not_for_me")
        var next = picks.slice()
        next.splice(pickList.currentIndex, 1)
        picks = next
        if (picks.length === 0) {
            goBack()
            return
        }
        pickList.currentIndex = Math.min(pickList.currentIndex, picks.length - 1)
    }

    Component.onCompleted: {
        reloadPicks()
        appCore.refreshTaterRecommendations()
    }

    Connections {
        target: appCore
        function onTaterRecommendationsChanged() {
            picksRoot.reloadPicks()
        }
    }

    AppBar {
        title: "TATER'S PICKS"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
    }

    Image {
        source: "../assets/images/mascots/usenet.png"
        anchors.right: parent.right
        anchors.rightMargin: root.sw * 0.035
        anchors.verticalCenter: parent.verticalCenter
        width: root.sw * 0.38
        height: root.sh * 0.62
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: root.sw * 0.055
        anchors.top: parent.top
        anchors.topMargin: root.sh * 0.18
        width: root.sw * 0.36
        height: root.sh * 0.13
        color: root.surfaceColor
        border.color: root.accentColor
        border.width: Math.max(2, root.sh * 0.005)

        Text {
            anchors.fill: parent
            anchors.margins: root.sh * 0.022
            text: picksRoot.selectedPick().reason || "I FOUND A FEW THINGS FOR MOVIE NIGHT."
            color: root.primaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.027
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
        }
    }

    ListView {
        id: pickList
        model: picksRoot.picks
        anchors.left: parent.left
        anchors.leftMargin: root.sw * 0.115625
        anchors.verticalCenter: parent.verticalCenter
        width: root.sw * 0.51
        height: root.sh * 0.48
        clip: true
        focus: true
        currentIndex: 0

        delegate: Item {
            id: pickDelegate
            required property var modelData
            required property int index
            width: pickList.width
            height: root.sh * 0.074

            Rectangle {
                anchors.fill: parent
                anchors.rightMargin: root.sw * 0.025
                color: pickList.currentIndex === pickDelegate.index ? root.accentColor : "transparent"
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: root.sw * 0.01
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - root.sw * 0.045
                spacing: root.sh * 0.003

                Text {
                    width: parent.width
                    text: pickDelegate.modelData.title || "UNTITLED"
                    color: pickList.currentIndex === pickDelegate.index ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.041
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: ((pickDelegate.modelData.media_type || "VIDEO") + "  •  "
                           + (pickDelegate.modelData.source || "TATER TUBE"))
                    color: pickList.currentIndex === pickDelegate.index ? root.surfaceColor : root.secondaryColor
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.021
                    elide: Text.ElideRight
                }
            }
        }

        Keys.onReturnPressed: picksRoot.playSelected()
        Keys.onEnterPressed: picksRoot.playSelected()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_D || event.key === Qt.Key_Delete) {
                picksRoot.dismissSelected()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
                picksRoot.goBack()
                event.accepted = true
            }
        }
    }

    Text {
        anchors.left: pickList.left
        anchors.top: pickList.bottom
        anchors.topMargin: root.sh * 0.03
        text: "[ENTER] PLAY    [D] NOT FOR ME    [BACK] RETURN"
        color: root.secondaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.022
    }
}
