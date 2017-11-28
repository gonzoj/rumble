local yt = require("youtube")

tube = {
	onLoad = function ()
		Rumble.Console.print("using " .. yt._VERSION .. "\n")
	end,
	onExit = function ()
		Rumble.Console.print("exiting" .. "\n")
		videos = nil
	end
}

videos = {}

local function split(s, d)
	d = string.gsub(d, "([%.%(%)%%%+%-%*%?%[%]%^%$])", "%%%0")
	local tokens = {}
	local i = 1
	while i <= string.len(s) do
		local j = i
		local token
		_, i, token = string.find(s, "(.-)" .. d, i)
		if (token) then
			table.insert(tokens, token)
		else
			table.insert(tokens, string.sub(s, j, -1))
			break
		end
		i = i + 1
	end
	return tokens
end

local function formatLength(l)
	local h = math.floor(l / 3600)
	local m = math.floor((l - h * 3600) / 60)
	local s = l - h * 3600 - m * 60
	return string.format("%02i:%02i:%02i", h, m, s)
end

local function unescape(s)
	s = string.gsub(s, "%b<>", "")
	s = string.gsub(s, "&amp;", "&")
	return s
end

local function escape(s)
	s = string.gsub(s, "&", "&amp;")
	s = string.gsub(s, "<", "&lt;")
	s = string.gsub(s, ">", "&gt;")
	return s
end

local function createLink(u)
	s = string.gsub(u, "&", "&amp;")
	s = "<a href=\"" .. u .. "\">" .. u .. "</a>"
	return s
end

local function getVideo(n)
	for i, v in ipairs(videos) do
		if v.name == n then
			return v, i
		end
	end
	return nil
end

local function playStream(u, s, v)
	User.sendTextMessage(u, "loading " .. createLink(s))
	local stream, error, ext, time, id, title, author, len, views, rating = yt.download(s, yt.format._MP4, yt.quality._MEDIUM)
	if not error then
		local file = title .. ext
		if getVideo(file) then
			file = file .. "*"
		end
		local volume
		if v then
			volume = tonumber(v)
		end
		if volume then
			Sound.Playback.startFromBuffer(file, stream, string.len(stream), nil, nil, volume)
		else
			Sound.Playback.startFromBuffer(file, stream, string.len(stream))
		end
		local v = { ["name"] = file, ["channel"] = User.getChannel(u), ["url"] = s, ["user"] = User.getName(u), ["title"] = title, ["len"] = len,
			["author"] = author, ["views"] = views, ["rating"] = rating, ["time"] = time }
		table.insert(videos, v)
		stream = nil
		collectgarbage()
	else
		Channel.sendTextMessage(User.getChannel(u), "request for " .. s .. " failed: package " .. yt._VERSION .. ": " .. error)
	end
end

function onCommandMessage(u, c)
	-- why the fuck does '==' even work?
	if not User.getPrivilege(u) == User.Privilege.Authenticated then
		User.sendTextMessage(u, "you have to be registered to use this feature")
		return
	end
	c = unescape(c)
	local full = c
	c = split(c, " ")
	if c[1] == "play" and c[2] then
		--playStream(u, c[2], c[3])
	elseif c[1] == "stop" then
		Sound.Playback.stop()
	elseif c[1] == "+" then
		Sound.Playback.volumeUp()
	elseif c[1] == "-" then
		Sound.Playback.volumeDown()
	elseif c[1] == "clear" then
		Sound.Playback.clear()
	else
		User.sendTextMessage(u, "invalid command " .. full)
		User.sendTextMessage(u, escape("try '+', '-', 'stop' or 'play <url>'"))
	end
end

function onTextMessage(u, m)
	m = unescape(m)
	if yt.verify(m) then
		--playStream(u, m)
	end
end

function onPlayback(n)
	local v, i = getVideo(n)
	if v then
		Channel.sendTextMessage(v.channel, "playing " .. createLink(v.url) .. " requested by " .. v.user .. ": "
			.. v.title .. " (" .. formatLength(tonumber(v.len)) .. ") uploaded by " .. v.author .. " with " .. v.views .. " views and an average rating of "
			.. string.format("%.2f", tonumber(v.rating)) .. "/5")
		Channel.sendTextMessage(v.channel, "took " .. v.time .. " seconds to load")
		table.remove(videos, i)
	end
end
