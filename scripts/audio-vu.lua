local LEVEL_PROP = "af-metadata/240mp_vu/lavfi.astats.Overall.RMS_level"
local UPDATE_INTERVAL = 0.08
local last_level = -1

local function clamp(value, min_value, max_value)
    if value < min_value then return min_value end
    if value > max_value then return max_value end
    return value
end

local function db_to_level(db)
    if not db then return 0 end

    local normalized = clamp((db + 60.0) / 52.0, 0.0, 1.0)
    return math.pow(normalized, 0.62)
end

mp.add_periodic_timer(UPDATE_INTERVAL, function()
    local db = mp.get_property_number(LEVEL_PROP, nil)
    local level = db_to_level(db)

    if math.abs(level - last_level) < 0.015 then return end
    last_level = level

    mp.commandv("script-message", "240mp-audio-level", string.format("%.3f", level))
end)

mp.register_event("end-file", function()
    mp.commandv("script-message", "240mp-audio-level", "0.000")
end)
