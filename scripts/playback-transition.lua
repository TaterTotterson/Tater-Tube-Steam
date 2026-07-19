local assdraw = require "mp.assdraw"

local overlay = mp.create_osd_overlay("ass-events")
local active = false

local function show_black()
    active = true
    local width, height = mp.get_osd_size()
    if width == 0 or height == 0 then
        return
    end

    local ass = assdraw.ass_new()
    ass:new_event()
    ass:pos(0, 0)
    ass:append("{\\an7\\bord0\\shad0\\1c&H000000&\\1a&H00&}")
    ass:draw_start()
    ass:rect_cw(0, 0, width, height)
    ass:draw_stop()

    overlay.res_x = width
    overlay.res_y = height
    overlay.data = ass.text
    overlay:update()
end

local function hide_black()
    active = false
    overlay:remove()
end

mp.register_script_message("240mp-transition-black", show_black)
mp.register_script_message("240mp-transition-black-hide", hide_black)

mp.register_event("start-file", show_black)
mp.register_event("file-loaded", function()
    if active then
        show_black()
    end
end)
mp.register_event("end-file", show_black)
mp.register_event("playback-restart", hide_black)
mp.register_event("shutdown", function()
    overlay:remove()
end)

-- Cover the initial player window immediately; file-loaded redraws once mpv
-- knows the final output size.
show_black()
