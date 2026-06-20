import QtQuick
import Components

FocusScope {
    id: otaRoot

    signal goBack()

    property var navParams: ({})
    property string moduleId: "com.240mp.ota"
    property var _moduleInfo: appCore.get_module_info(moduleId)
    property string moduleName: _moduleInfo.name || "OVER THE AIR"
    property string moduleIcon: _moduleInfo.icon || ""

    property var channels: []
    property int currentIndex: -1
    property string pendingChannelId: ""
    property string statusText: "LOADING CHANNELS..."
    property string serverName: ""
    property bool leaving: false
    property bool hasStartedPlayback: false
    property bool pendingForceTranscode: false
    property bool currentForceTranscode: false

    focus: true

    function newSessionId() {
        var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        var id = ""
        for (var i = 0; i < 12; i++) id += chars[Math.floor(Math.random() * chars.length)]
        return id
    }

    function channelLabel(channel) {
        if (!channel) return "NO CHANNEL"
        var number = channel.number || ""
        var name = channel.name || ""
        if (number !== "" && name !== "") return "CH " + number + "  " + name
        if (number !== "") return "CH " + number
        if (name !== "") return name
        return "CHANNEL"
    }

    function forceTranscodeEnabled() {
        var quality = appCore.get_setting("com.240mp.emby_jellyfin", "video_quality") || "auto"
        return quality !== "auto"
    }

    function tuneIndex(index) {
        if (channels.length === 0) return
        if (index < 0) index = channels.length - 1
        if (index >= channels.length) index = 0

        currentIndex = index
        var channel = channels[currentIndex]
        if (!channel || !channel.id) return

        pendingChannelId = channel.id
        statusText = "TUNING " + channelLabel(channel)
        pendingForceTranscode = forceTranscodeEnabled()
        mpvController.sendScriptMessage("240mp-ota-channel", statusText)
        appCore.save_setting(moduleId, "last_channel_id", channel.id)
        embyBackend.request_live_tv_stream(channel.id, newSessionId(), pendingForceTranscode)
    }

    function tuneRelative(delta) {
        if (channels.length === 0) return
        tuneIndex(currentIndex + delta)
    }

    function exitOta() {
        leaving = true
        mpvController.stop()
        goBack()
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Up) {
            tuneRelative(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            tuneRelative(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            exitOta()
            event.accepted = true
        } else if (event.key === Qt.Key_Space) {
            mpvController.sendKey("SPACE")
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            mpvController.sendKey("LEFT")
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            mpvController.sendKey("RIGHT")
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            mpvController.sendKey("ENTER")
            event.accepted = true
        }
    }

    Connections {
        target: embyBackend

        function onLiveTvChannelsLoaded(items) {
            channels = items || []
            if (channels.length === 0) {
                statusText = "NO OTA CHANNELS"
                return
            }

            var restoreId = appCore.get_setting(moduleId, "last_channel_id") || ""
            var restoreIndex = 0
            for (var i = 0; i < channels.length; i++) {
                if (channels[i].id === restoreId) {
                    restoreIndex = i
                    break
                }
            }
            tuneIndex(restoreIndex)
        }

        function onLiveTvStreamReady(channelId, url, httpHeaderFields) {
            if (channelId !== pendingChannelId) return

            var channel = channels[currentIndex]
            var label = channelLabel(channel)
            statusText = label
            hasStartedPlayback = true
            currentForceTranscode = pendingForceTranscode
            mpvController.loadAndPlay(url, 0.0, 0, -1, [], false, -1, 0.0,
                                      httpHeaderFields, false, "ota", false, label)
        }

        function onErrorOccurred(msg) {
            statusText = msg || "OTA ERROR"
        }
    }

    Connections {
        target: mpvController
        function onPlaybackFinished(finalPositionMs, finalDurationMs) {
            if (!leaving && hasStartedPlayback)
                goBack()
        }
        function onPlaybackFailed() {
            if (!currentForceTranscode && pendingChannelId !== "") {
                statusText = "RETRYING TRANSCODE"
                pendingForceTranscode = true
                mpvController.sendScriptMessage("240mp-ota-channel", statusText)
                embyBackend.request_live_tv_stream(pendingChannelId, newSessionId(), true)
                return
            }
            statusText = "OTA PLAYBACK FAILED"
        }
        function onScriptMessageReceived(message, arg) {
            if (message !== "240mp-ota-channel-step")
                return

            var delta = parseInt(arg)
            if (isNaN(delta) || delta === 0)
                return

            tuneRelative(delta)
        }
    }

    Component.onCompleted: {
        serverName = embyBackend.get_active_server_name()
        if (embyBackend.get_auth_state() !== "authed") {
            statusText = "SIGN IN TO VIDEO ON DEMAND"
            return
        }
        embyBackend.load_live_tv_channels()
    }

    Component.onDestruction: {
        if (!leaving)
            mpvController.stop()
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    AppBar {
        iconSource: otaRoot.moduleIcon
        title: otaRoot.moduleName
        subtitle: otaRoot.serverName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.125
        anchors.leftMargin: root.sw * 0.125
        visible: !hasStartedPlayback
    }

    Text {
        text: statusText
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.centerIn: parent
        horizontalAlignment: Text.AlignHCenter
        width: root.sw * 0.8
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.05
    }

    Text {
        text: root.hints.back + ":BACK  CH +/-:UP/DOWN"
        color: root.tertiaryColor
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.1041667
        anchors.leftMargin: root.sw * 0.125
        font.pixelSize: root.sh * 0.0333333
        visible: !hasStartedPlayback
    }
}
