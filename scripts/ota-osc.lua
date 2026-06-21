local assdraw = require 'mp.assdraw'

local overlay = mp.create_osd_overlay("ass-events")
local hide_timer = nil
local menu_timer = nil
local DISPLAY_SECONDS = 7.0
local MENU_SECONDS = 7.0
local latest_label = ""
local menu_visible = false

local function ass_escape(text)
    return tostring(text):gsub("\\", "\\\\"):gsub("{", "\\{"):gsub("}", "\\}")
end

local function hide()
    if hide_timer then
        hide_timer:kill()
        hide_timer = nil
    end
    if menu_timer then
        menu_timer:kill()
        menu_timer = nil
    end
    menu_visible = false
    overlay:remove()
end

local function draw_box(ass, x, y, w, h, alpha)
    ass:new_event()
    ass:pos(x, y)
    ass:append(string.format("{\\bord0\\shad0\\1c&H000000&\\1a%s}", alpha or "&H55&"))
    ass:draw_start()
    ass:rect_cw(0, 0, w, h)
    ass:draw_stop()
end

local function draw_text(ass, x, y, anchor, text, fs)
    ass:new_event()
    ass:append(string.format(
        "{\\an%d\\pos(%d,%d)\\fnVCR OSD Mono\\fs%d\\1c&HFFFFFF&\\1a&H00&\\bord0\\shad0}%s",
        anchor,
        x,
        y,
        fs,
        ass_escape(text)))
end

local function draw_button(ass, x, y, w, h, label, fs)
    draw_box(ass, x, y, w, h, "&H55&")
    draw_text(ass, x + math.floor(w / 2), y + math.floor(h / 2), 5, label, fs)
end

local function draw_overlay(label, with_menu)
    if not label or label == "" then return false end

    local ww, wh = mp.get_osd_size()
    if ww == 0 or wh == 0 then return false end

    local margin_x = math.floor(ww * 0.08)
    local margin_y = math.floor(wh * 0.11)
    local pad_x = math.floor(ww * 0.018)
    local pad_y = math.floor(wh * 0.012)
    local fs = math.max(18, math.floor(wh * 0.065))
    local box_w = math.min(math.floor(ww * 0.72), math.floor(#label * fs * 0.56 + pad_x * 2))
    local box_h = math.floor(fs * 1.35 + pad_y * 2)
    local box_x = ww - margin_x - box_w
    local text_x = ww - margin_x - pad_x

    local ass = assdraw.ass_new()

    draw_box(ass, box_x, margin_y, box_w, box_h, "&H55&")
    draw_text(ass, text_x, margin_y + math.floor(box_h / 2), 6, label, fs)

    if with_menu then
        local lm = math.floor(ww * 0.12)
        local rm = math.floor(ww * 0.88)
        local menu_w = rm - lm
        local menu_fs = math.max(14, math.floor(wh * 0.0333333))
        local title_fs = math.max(18, math.floor(wh * 0.05))
        local menu_y = math.floor(wh * 0.70)
        local menu_h = math.floor(wh * 0.18)
        local row_y = menu_y + math.floor(menu_h * 0.30)
        local btn_y = menu_y + math.floor(menu_h * 0.58)
        local btn_h = math.floor(menu_fs * 1.65)
        local gap = math.floor(menu_w * 0.025)
        local btn_w = math.floor((menu_w - gap * 3) / 4)

        draw_box(ass, lm, menu_y, menu_w, menu_h, "&H66&")
        draw_text(ass, lm + math.floor(menu_w * 0.03), row_y, 4, "LIVE TV", title_fs)
        draw_text(ass, rm - math.floor(menu_w * 0.03), row_y, 6, label, menu_fs)

        local bx = lm + math.floor(menu_w * 0.03)
        local usable_w = menu_w - math.floor(menu_w * 0.06)
        btn_w = math.floor((usable_w - gap * 3) / 4)
        draw_button(ass, bx, btn_y, btn_w, btn_h, "CH -", menu_fs)
        bx = bx + btn_w + gap
        draw_button(ass, bx, btn_y, btn_w, btn_h, "LAST", menu_fs)
        bx = bx + btn_w + gap
        draw_button(ass, bx, btn_y, btn_w, btn_h, "TUNE", menu_fs)
        bx = bx + btn_w + gap
        draw_button(ass, bx, btn_y, btn_w, btn_h, "CH +", menu_fs)
    end

    overlay.res_x = ww
    overlay.res_y = wh
    overlay.data = ass.text
    overlay:update()

    if hide_timer then hide_timer:kill() end
    hide_timer = mp.add_timeout(DISPLAY_SECONDS, hide)
    return true
end

local function start_label_timeout()
    if hide_timer then hide_timer:kill() end
    hide_timer = mp.add_timeout(DISPLAY_SECONDS, function()
        if not menu_visible then
            overlay:remove()
        end
    end)
end

local function show_label(label)
    if not label or label == "" then return end
    latest_label = label

    draw_overlay(label, menu_visible)
    if not menu_visible then
        start_label_timeout()
    end
    for _, delay in ipairs({0.2, 0.7, 1.4}) do
        mp.add_timeout(delay, function()
            if latest_label == label then
                draw_overlay(label, menu_visible)
            end
        end)
    end
end

local function hide_menu()
    if menu_timer then
        menu_timer:kill()
        menu_timer = nil
    end
    menu_visible = false
    if latest_label ~= "" then
        draw_overlay(latest_label, false)
        start_label_timeout()
    else
        overlay:remove()
    end
end

local function show_menu()
    local title = mp.get_property("media-title", "")
    if latest_label == "" and title ~= "" then
        latest_label = title
    end
    if latest_label == "" then
        latest_label = "AIR"
    end

    if hide_timer then
        hide_timer:kill()
        hide_timer = nil
    end
    if menu_timer then
        menu_timer:kill()
    end
    menu_visible = true
    draw_overlay(latest_label, true)
    menu_timer = mp.add_timeout(MENU_SECONDS, hide_menu)
end

local function toggle_menu()
    if menu_visible then
        hide_menu()
    else
        show_menu()
    end
end

mp.register_script_message("240mp-ota-channel", show_label)

mp.register_event("file-loaded", function()
    local title = mp.get_property("media-title", "")
    if title ~= "" then
        show_label(title)
    end
end)

local function tune_relative(delta)
    mp.commandv("script-message", "240mp-ota-channel-step", tostring(delta))
end

local function tune_now()
    mp.commandv("script-message", "240mp-ota-tune-now")
end

local function tune_last()
    mp.commandv("script-message", "240mp-ota-last-channel")
end

mp.add_forced_key_binding("UP", "ota-channel-up", function() tune_relative(1) end)
mp.add_forced_key_binding("DOWN", "ota-channel-down", function() tune_relative(-1) end)
mp.add_forced_key_binding("LEFT", "ota-last-channel", tune_last)
mp.add_forced_key_binding("PREV", "ota-last-channel-prev", tune_last)
mp.add_forced_key_binding("MENU", "ota-menu", toggle_menu)
mp.add_forced_key_binding("ENTER", "ota-tune-enter", tune_now)
mp.add_forced_key_binding("KP_ENTER", "ota-tune-kp-enter", tune_now)
mp.add_key_binding("ESC", "ota-esc", function() mp.command("quit") end)
mp.add_key_binding("BS", "ota-bs", function() mp.command("quit") end)
