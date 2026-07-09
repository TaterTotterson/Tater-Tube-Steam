import QtQuick
import Components

FocusScope {
    id: tvRoot

    property var navParams: ({})

    signal goBack()

    property string youtubeModuleId: "com.240mp.youtube_playlist"
    property var sourceChannels: []
    property var channels: []
    property var commercialPlaylists: []
    property var commercialPool: []
    property var commercialLoadQueue: []
    property var currentCommercialLoad: ({})
    property int currentIndex: -1
    property int previousIndex: -1
    property int currentScheduleIndex: -1
    property double startedAtMs: 0
    property bool loading: true
    property bool leaving: false
    property bool tuningStaticVisible: true
    property bool noSignalVisible: false
    property bool stoppingForTune: false
    property bool streamStarted: false
    property bool streamRequestActive: false
    property bool loadingCommercials: false
    property string pendingRequestId: ""
    property var pendingPlayback: ({})
    property int requestSerial: 0
    property int tuneDelayMs: 1200
    property string statusText: "LOADING TV MODE"

    focus: true

    function padChannelNumber(value) {
        var text = String(value)
        return text.length < 2 ? "0" + text : text
    }

    function shuffleList(items) {
        var shuffled = (items || []).slice()
        for (var i = shuffled.length - 1; i > 0; i--) {
            var j = Math.floor(Math.random() * (i + 1))
            var tmp = shuffled[i]
            shuffled[i] = shuffled[j]
            shuffled[j] = tmp
        }
        return shuffled
    }

    function sharedSettingValue(key, fallback) {
        var value = appCore.get_setting(youtubeModuleId, key)
        if (value === undefined || value === null || value === "")
            return fallback
        return value
    }

    function commercialsEnabled() {
        var value = sharedSettingValue("tv_mode_commercials", true)
        return value === true || value === "ON" || value === "true" || value === "1"
    }

    function publicAccessPlaybackQuality() {
        return sharedSettingValue("playback_quality", "360p")
    }

    function listSetting(key) {
        var value = appCore.get_setting(youtubeModuleId, key)
        return Array.isArray(value) ? value : []
    }

    function loadCommercialPlaylists() {
        var saved = listSetting("commercial_playlists")
        var rows = []
        var seen = ({})
        for (var i = 0; i < saved.length; i++) {
            var item = Object.assign({}, saved[i] || ({}))
            var input = (item.input || item.url || "").trim()
            if (input === "" || seen[input] === true)
                continue
            seen[input] = true
            item.input = input
            item.title = item.title || ("COMMERCIALS " + (rows.length + 1))
            rows.push(item)
        }
        commercialPlaylists = rows
    }

    function durationSeconds(item, fallback) {
        var raw = item ? item.duration : 0
        var value = typeof raw === "number" ? raw : parseFloat(raw)
        if (isNaN(value) || value <= 0)
            value = fallback
        if (value > 10000)
            value = value / 1000.0
        return Math.max(5, value)
    }

    function channelLabel(channel) {
        if (!channel) return "CH --"
        var title = String(channel.title || "").trim()
        return "CH " + (channel.number || "--") + (title !== "" ? " " + title : "")
    }

    function randomCommercial() {
        if (commercialPool.length === 0)
            return null
        return commercialPool[Math.floor(Math.random() * commercialPool.length)]
    }

    function appendScheduleItem(schedule, source, kind, startAt) {
        if (!source)
            return startAt
        if (kind === "commercial") {
            if (!source.url)
                return startAt
        } else if (!source.ratingKey) {
            return startAt
        }

        var fallback = kind === "commercial" ? 30 : (kind === "movie" ? 5400 : 1500)
        var duration = durationSeconds(source, fallback)
        var item = Object.assign({}, source)
        item.kind = kind
        item.duration = duration
        item.start = startAt
        item.end = startAt + duration
        schedule.push(item)
        return item.end
    }

    function appendCommercialBreak(schedule, startAt) {
        if (!commercialsEnabled() || commercialPool.length === 0)
            return startAt

        var total = startAt
        var count = 1 + Math.floor(Math.random() * 3)
        for (var i = 0; i < count; i++) {
            var commercial = randomCommercial()
            if (commercial)
                total = appendScheduleItem(schedule, commercial, "commercial", total)
        }
        return total
    }

    function buildMovieSchedule(programs) {
        var schedule = []
        var total = 0
        var movies = shuffleList(programs || [])
        for (var i = 0; i < movies.length; i++) {
            total = appendScheduleItem(schedule, movies[i], "movie", total)
            total = appendCommercialBreak(schedule, total)
        }
        return { schedule: schedule, totalDuration: total }
    }

    function buildTvSchedule(groups) {
        var schedule = []
        var total = 0
        var states = []
        var totalEpisodes = 0

        for (var i = 0; i < (groups || []).length; i++) {
            var group = groups[i] || ({})
            var episodes = group.episodes || []
            if (episodes.length === 0)
                continue
            states.push({
                group: group,
                episodes: episodes,
                nextIndex: Math.floor(Math.random() * episodes.length)
            })
            totalEpisodes += episodes.length
        }

        if (states.length === 0 || totalEpisodes <= 0)
            return { schedule: [], totalDuration: 0 }

        var previousState = -1
        for (var slot = 0; slot < totalEpisodes; slot++) {
            var stateIndex = Math.floor(Math.random() * states.length)
            if (previousState >= 0 && Math.random() < 0.28)
                stateIndex = previousState

            var state = states[stateIndex]
            var episode = state.episodes[state.nextIndex % state.episodes.length]
            state.nextIndex++
            total = appendScheduleItem(schedule, episode, "episode", total)
            total = appendCommercialBreak(schedule, total)
            previousState = stateIndex
        }

        return { schedule: schedule, totalDuration: total }
    }

    function buildReadyChannels(loadedChannels) {
        var ready = []
        var shuffled = shuffleList(loadedChannels || [])
        for (var i = 0; i < shuffled.length; i++) {
            var source = shuffled[i] || ({})
            var scheduleData = source.channelType === "tv"
                ? buildTvSchedule(source.programs || [])
                : buildMovieSchedule(source.programs || [])
            if (!scheduleData.schedule || scheduleData.schedule.length === 0 ||
                    scheduleData.totalDuration <= 0) {
                continue
            }

            ready.push({
                number: padChannelNumber(ready.length + 2),
                title: source.title || "VIDEO",
                channelType: source.channelType || "movie",
                schedule: scheduleData.schedule,
                totalDuration: scheduleData.totalDuration
            })
        }

        channels = ready
        loading = false
        currentScheduleIndex = -1
        previousIndex = -1
        startedAtMs = Date.now()

        if (channels.length === 0) {
            tuningStaticVisible = false
            noSignalVisible = true
            statusText = "NO VOD TV CHANNELS"
            return
        }

        tuneIndex(0, false)
    }

    function startCommercialLoad(loadedChannels) {
        sourceChannels = loadedChannels || []
        loadCommercialPlaylists()
        commercialPool = []
        commercialLoadQueue = []
        currentCommercialLoad = ({})

        if (!commercialsEnabled() || commercialPlaylists.length === 0) {
            buildReadyChannels(sourceChannels)
            return
        }

        for (var i = 0; i < commercialPlaylists.length; i++)
            commercialLoadQueue.push(commercialPlaylists[i])

        loadingCommercials = true
        loading = true
        tuningStaticVisible = true
        statusText = "LOADING COMMERCIALS"
        loadNextCommercialPlaylist()
    }

    function loadNextCommercialPlaylist() {
        if (!loadingCommercials)
            return
        if (commercialLoadQueue.length === 0) {
            loadingCommercials = false
            buildReadyChannels(sourceChannels)
            return
        }

        currentCommercialLoad = commercialLoadQueue.shift() || ({})
        statusText = "LOADING " + (currentCommercialLoad.title || "COMMERCIALS")
        youtubePlaylistBackend.load_playlist(currentCommercialLoad.input || currentCommercialLoad.url || "")
    }

    function handleCommercialPlaylistLoaded(items) {
        var playlistItems = items || []
        for (var i = 0; i < playlistItems.length; i++) {
            var commercial = Object.assign({}, playlistItems[i])
            if (!commercial.url)
                continue
            commercial.commercial = true
            commercialPool.push(commercial)
        }
        currentCommercialLoad = ({})
        loadNextCommercialPlaylist()
    }

    function handleCommercialPlaylistError() {
        currentCommercialLoad = ({})
        loadNextCommercialPlaylist()
    }

    function selectedChannel() {
        if (currentIndex < 0 || currentIndex >= channels.length)
            return null
        return channels[currentIndex]
    }

    function findScheduleItem(channel) {
        if (!channel || !channel.schedule || channel.schedule.length === 0 ||
                channel.totalDuration <= 0) {
            return null
        }

        var elapsed = Math.max(0, (Date.now() - startedAtMs) / 1000.0)
        var position = elapsed % channel.totalDuration
        for (var i = 0; i < channel.schedule.length; i++) {
            var item = channel.schedule[i]
            if (position >= item.start && position < item.end)
                return { item: item, index: i, offset: Math.max(0, position - item.start) }
        }

        return { item: channel.schedule[0], index: 0, offset: 0 }
    }

    function showStaticForChannel(channel) {
        tuningStaticVisible = true
        noSignalVisible = false
        streamStarted = false
        streamRequestActive = false
        pendingRequestId = ""
        pendingPlayback = ({})
        youtubePlaylistBackend.cancel_video_stream_resolve()
        statusText = channelLabel(channel)

        if (mpvController.running) {
            stoppingForTune = true
            mpvController.stop()
        }
    }

    function requestSelectedStream() {
        tuneTimer.stop()
        if (loading || channels.length === 0)
            return

        var channel = selectedChannel()
        var resolved = findScheduleItem(channel)
        if (!resolved || !resolved.item ||
                (resolved.item.kind === "commercial"
                 ? !resolved.item.url
                 : !resolved.item.ratingKey)) {
            tuningStaticVisible = false
            noSignalVisible = true
            statusText = "VOD TV CHANNEL EMPTY"
            return
        }

        currentScheduleIndex = resolved.index
        streamRequestActive = true
        var label = channelLabel(channel)
        statusText = label

        var requestId = "vodtv-" + (++requestSerial)
        pendingRequestId = requestId
        pendingPlayback = {
            item: resolved.item,
            offset: resolved.offset || 0.0,
            label: label
        }
        if (resolved.item.kind === "commercial") {
            var format = youtubePlaylistBackend.ytdl_format_for_quality(publicAccessPlaybackQuality())
            youtubePlaylistBackend.resolve_video_stream(requestId,
                                                        resolved.item.url || "",
                                                        publicAccessPlaybackQuality())
            pendingPlayback.format = format
        } else {
            embyBackend.prepare_vod_tv_stream(requestId, resolved.item)
        }
    }

    function launchPlayback(url, httpHeaderFields, offset, label, allowYtdl, format) {
        if (!url) {
            noSignalVisible = true
            tuningStaticVisible = false
            streamRequestActive = false
            statusText = "VOD TV PLAYBACK FAILED"
            return
        }

        streamStarted = true
        stoppingForTune = false
        streamRequestActive = false
        noSignalVisible = false
        mpvController.loadAndPlay(url, offset || 0.0, 0, -1, [], false, -1, 0.0,
                                  httpHeaderFields || "", false, "ota", false, label || statusText,
                                  false, !!allowYtdl, format || "")
    }

    function playNextScheduleItem() {
        if (channels.length === 0)
            return

        var channel = selectedChannel()
        if (!channel || !channel.schedule || channel.schedule.length === 0) {
            requestSelectedStream()
            return
        }

        var nextIndex = currentScheduleIndex + 1
        if (nextIndex < 0 || nextIndex >= channel.schedule.length)
            nextIndex = 0

        var nextItem = channel.schedule[nextIndex]
        startedAtMs = Date.now() - Math.max(0, nextItem.start) * 1000.0
        showStaticForChannel(channel)
        requestSelectedStream()
    }

    function tuneIndex(index, immediate) {
        if (loading || channels.length === 0)
            return
        if (index < 0)
            index = channels.length - 1
        if (index >= channels.length)
            index = 0
        if (currentIndex >= 0 && currentIndex !== index)
            previousIndex = currentIndex

        currentIndex = index
        currentScheduleIndex = -1
        showStaticForChannel(channels[currentIndex])
        if (immediate)
            requestSelectedStream()
        else
            tuneTimer.restart()
    }

    function tuneRelative(delta, immediate) {
        tuneIndex(currentIndex + delta, immediate)
    }

    function tuneNow() {
        if (loading)
            return
        requestSelectedStream()
    }

    function tuneLastChannel() {
        if (previousIndex < 0 || previousIndex >= channels.length)
            return
        tuneIndex(previousIndex, false)
    }

    function exitTvMode() {
        leaving = true
        tuneTimer.stop()
        pendingRequestId = ""
        youtubePlaylistBackend.cancel_video_stream_resolve()
        if (mpvController.running)
            mpvController.stop()
        goBack()
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            exitTvMode()
            event.accepted = true
        } else if (loading) {
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            tuneRelative(1, false)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            tuneRelative(-1, false)
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            tuneLastChannel()
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            mpvController.sendKey("RIGHT")
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            tuneNow()
            event.accepted = true
        } else if (event.key === Qt.Key_Menu) {
            mpvController.sendKey("MENU")
            event.accepted = true
        }
    }

    Timer {
        id: tuneTimer
        interval: tvRoot.tuneDelayMs
        repeat: false
        onTriggered: tvRoot.requestSelectedStream()
    }

    Connections {
        target: embyBackend

        function onVodTvChannelsLoaded(loadedChannels) {
            if (tvRoot.leaving)
                return
            tvRoot.startCommercialLoad(loadedChannels || [])
        }

        function onVodTvStreamReady(requestId, item, url, httpHeaderFields) {
            if (requestId !== tvRoot.pendingRequestId)
                return
            var pending = tvRoot.pendingPlayback || ({})
            tvRoot.pendingRequestId = ""
            tvRoot.pendingPlayback = ({})
            tvRoot.launchPlayback(url, httpHeaderFields,
                                  pending.offset || 0.0,
                                  pending.label || tvRoot.statusText,
                                  false,
                                  "")
        }

        function onVodTvStreamFailed(requestId, message) {
            if (requestId !== tvRoot.pendingRequestId)
                return
            tvRoot.pendingRequestId = ""
            tvRoot.pendingPlayback = ({})
            tvRoot.streamRequestActive = false
            tvRoot.tuningStaticVisible = false
            tvRoot.noSignalVisible = true
            tvRoot.statusText = message || "VOD TV PLAYBACK FAILED"
        }

        function onErrorOccurred(message) {
            if (!tvRoot.loading)
                return
            tvRoot.loading = false
            tvRoot.tuningStaticVisible = false
            tvRoot.noSignalVisible = true
            tvRoot.statusText = message || "VOD TV FAILED"
        }
    }

    Connections {
        target: youtubePlaylistBackend

        function onPlaylistLoaded(title, items) {
            if (!tvRoot.loadingCommercials)
                return
            tvRoot.handleCommercialPlaylistLoaded(items || [])
        }

        function onErrorOccurred(message) {
            if (tvRoot.loadingCommercials) {
                tvRoot.handleCommercialPlaylistError()
                return
            }

            if (tvRoot.pendingRequestId === "")
                return
            tvRoot.pendingRequestId = ""
            tvRoot.pendingPlayback = ({})
            tvRoot.streamRequestActive = false
            tvRoot.tuningStaticVisible = false
            tvRoot.noSignalVisible = true
            tvRoot.statusText = message || "COMMERCIAL PLAYBACK FAILED"
        }

        function onVideoStreamResolved(requestId, result) {
            if (requestId !== tvRoot.pendingRequestId)
                return

            var pending = tvRoot.pendingPlayback || ({})
            tvRoot.pendingRequestId = ""
            tvRoot.pendingPlayback = ({})

            var directUrl = result && result.ok === true ? (result.url || "") : ""
            if (directUrl !== "") {
                tvRoot.launchPlayback(directUrl, "", pending.offset || 0.0,
                                      pending.label || tvRoot.statusText,
                                      false, "")
                return
            }

            var item = pending.item || ({})
            tvRoot.launchPlayback(item.url || "", "", pending.offset || 0.0,
                                  pending.label || tvRoot.statusText,
                                  true, pending.format || "")
        }
    }

    Connections {
        target: mpvController

        function onPositionChanged(ms) {
            if (ms > 0 && tvRoot.streamStarted)
                tvRoot.tuningStaticVisible = false
        }

        function onPlaybackFinishedNaturally(finalPositionMs, finalDurationMs) {
            if (tvRoot.leaving)
                return
            tvRoot.playNextScheduleItem()
        }

        function onPlaybackFinished(finalPositionMs, finalDurationMs) {
            if (tvRoot.stoppingForTune) {
                tvRoot.stoppingForTune = false
                return
            }
            if (!tvRoot.leaving && tvRoot.streamStarted)
                tvRoot.exitTvMode()
        }

        function onPlaybackFailed() {
            if (tvRoot.stoppingForTune) {
                tvRoot.stoppingForTune = false
                return
            }
            tvRoot.streamRequestActive = false
            tvRoot.tuningStaticVisible = false
            tvRoot.noSignalVisible = true
            tvRoot.statusText = "VOD TV PLAYBACK FAILED"
        }

        function onScriptMessageReceived(message, arg) {
            if (message === "240mp-ota-file-loaded") {
                tvRoot.tuningStaticVisible = false
                tvRoot.streamStarted = true
                return
            }

            if (message === "240mp-ota-tune-now") {
                tvRoot.tuneNow()
                return
            }

            if (message === "240mp-ota-last-channel") {
                tvRoot.tuneLastChannel()
                return
            }

            if (message !== "240mp-ota-channel-step")
                return

            var delta = parseInt(arg)
            if (isNaN(delta) || delta === 0)
                return
            tvRoot.tuneRelative(delta, false)
        }
    }

    Component.onCompleted: {
        embyBackend.load_vod_tv_channels(false)
    }

    Component.onDestruction: {
        if (!leaving && mpvController.running)
            mpvController.stop()
    }

    StaticBackground {
        anchors.fill: parent
        visible: tvRoot.tuningStaticVisible
        running: visible
    }

    Rectangle {
        anchors.fill: parent
        color: tvRoot.tuningStaticVisible ? "transparent" : "black"
    }

    NoSignalScreen {
        anchors.fill: parent
        visible: tvRoot.noSignalVisible
        inputLabel: "VIDEO"
        message: "NO SIGNAL"
        detail: tvRoot.statusText
        z: 4
    }

    Text {
        visible: tvRoot.loading
        text: tvRoot.statusText
        color: root.primaryColor
        font.family: root.globalFont
        font.capitalization: Font.AllUppercase
        anchors.centerIn: parent
        horizontalAlignment: Text.AlignHCenter
        width: root.sw * 0.78
        wrapMode: Text.WordWrap
        font.pixelSize: root.sh * 0.045
        z: 6
    }

    Rectangle {
        visible: tvRoot.tuningStaticVisible && !tvRoot.loading
        z: 5
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: root.sh * 0.09
        anchors.rightMargin: root.sw * 0.07
        width: Math.min(root.sw * 0.66,
                        Math.max(channelText.implicitWidth + root.sw * 0.04,
                                 root.sw * 0.18))
        height: root.sh * 0.085
        color: "#60000000"
        border.color: root.primaryColor
        border.width: Math.max(1, root.sh * 0.004)

        Text {
            id: channelText
            anchors.centerIn: parent
            width: parent.width - root.sw * 0.025
            text: tvRoot.statusText
            color: root.primaryColor
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            font.pixelSize: root.sh * 0.055
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
}
