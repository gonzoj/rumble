local base = _G
local io = require("io")
local os = require("os")
local string = require("string")
local table = require("table")

local http = require("socket.http")
local url = require("socket.url")

module("youtube")

_VERSION = "YouTube 1.0.0"

-- public constants

format = {
	_WEBM = { mime = "video/webm", extension = ".webm" },
	_MP4 = { mime = "video/mp4", extension = ".mp4" },
	_FLV = { mime = "video/x-flv", extension = ".flv" },
	_3GP = { mime = "video/3gpp", extension = ".3gp" },
	_ANY = { mime = "any", extension = ".*" }
}

quality = {
	_LOWEST = 0,
	_LOW = 1,
	small = 1,
	_MEDIUM = 2,
	medium = 2,
	large = 3,
	_HIGH = 4,
	hd720 = 4,
	hd1080 = 5,
	_BEST = 6
}

-- public fields

verbose = false

-- convenience functions

local function kpairs(t, o)
	local keys = {}
	for key in base.pairs(t) do
		table.insert(keys, key)
	end
	table.sort(keys, o)
	local i = 0
	local function iterate()
		i = i + 1
		if keys[i] == nil then
			return nil
		else
			return keys[i], t[keys[i]]
		end
	end
	return iterate
end

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

-- Stream class

local function serializeStream(s, d)
	local params, param = split(d, "&")
	for _, param in base.ipairs(params) do
		local key, value
		_, _, key, value = string.find(param, "(.-)=(.*)")
		base.assert(key and value, "assertion failed: unknown stream parameter format " .. param)
		s[key] = value
	end
end

local function serializeStreamType(v)
	local mime, codecs
	_, _, mime, codecs = string.find(v, "(.-);%+codecs=\"(.*)\"")
	local type = {}
	type.getFormat = function(self) for _, f in kpairs(format) do if f.mime == self.mime then return f end end end
	if mime then
		type.mime = mime
	else
		type.mime = v
	end
	if codecs then
		type.codecs = split(codecs, ",+")
	else
		type.codecs = nil
	end
	return type
end

local function dumpStream(self)
	io.write("[" .. self.itag .. "] " .. self.quality .. " quality stream of type " .. self.type.mime)
	if self.type.codecs then
		io.write(" (")
		for i, codec in base.ipairs(self.type.codecs) do
			if i > 1 then
				io.write(", ")
			end
			io.write(codec)
		end
		io.write(")")
	end
	base.print(":\n")
	base.print(self.url .. "&signature=" .. self.sig .. " (fallback host: " .. self.fallback_host .. ")")
end

local function getStream(self)
	local resource = self.url .. "&signature=" .. self.sig
	local data, error = http.request(resource)
	if data then
		return data
	else
		return nil, "failed to retrieve resource ".. resource .. ": " .. error
	end
end

local function newStream(d)
	local stream = {}
	stream.dump = dumpStream
	stream.getStream = getStream
	local mt = {
		__newindex = function(t, k, v)
			v = url.unescape(v)
			if k == "type" then
				v = serializeStreamType(v)
				if not v:getFormat() then
					base.error("unsupported stream format " .. v.mime, 0)
				end
			end
			if k == "quality" and not quality[v] then
				base.error("unsupported stream quality " .. v, 0)
			end
			base.rawset(t, k, v)
		end,
		__index = function(t, k)
			base.error("undefined access to stream parameter " .. base.tostring(k), 2)
		end
	}
	base.setmetatable(stream, mt)
	serializeStream(stream, d)
	return stream
end

local Stream = { new = newStream }

-- StreamMap class

local function serializeStreamMap(m, d)
	local streams, stream = split(d, ",")
	for i, stream in base.ipairs(streams) do
		m[i] = stream
	end
end

local function dumpStreamMap(self)
	for i, stream in base.ipairs(self) do
		stream:dump()
		if i < #self then
			base.print()
		end
	end
end

local function filterStreamMap(self, k, v, f)
	if not f then
		f = function(x, y) return x == y end
	end
	local match = false
	for _, stream in base.ipairs(self) do
		if f(stream[k], v) then
			match = true
			break
		end
	end
	if match then
		for i = #self, 1, - 1 do
			if not f(self[i][k], v) then
				table.remove(self, i)
			end
		end
	end
end

local function iterateStreamMap(self)
	local i = 0
	local function iterate()
		i = i + 1
		return self[i]
	end
	return iterate

end

local function getBestQuality(self)
	local q
	for _, stream in base.ipairs(self) do
		if not q then
			q = quality[stream.quality]
		elseif q < quality[stream.quality] then
			q = quality[stream.quality]
		end
	end
	return q
end

local function newStreamMap(d)
	local map = {}
	map.dump = dumpStreamMap
	map.filter = filterStreamMap
	map.iterate = iterateStreamMap
	map.getBestQuality = getBestQuality
	local mt = {
		__newindex = function(t, k, v)
			if base.type(v) == "string" then
				v = Stream.new(v)
			end
			base.rawset(t, k, v)
		end
	}
	base.setmetatable(map, mt)
	serializeStreamMap(map, d)
	return map
end

local StreamMap = { new = newStreamMap }

-- FormatList class

local function serializeFormatList(l, d)
	local formats, format = split(d, ",")
	for _, format in base.ipairs(formats) do
		local itag, width, height
		_, _, itag, width, height = string.find(format, "(%d+)/(%d+)x(%d+)/.*")
		base.assert(itag and width and height, "assertion failed: unknown format definition " .. format)
		l[itag] = { ["width"] = base.tonumber(width), ["height"] = base.tonumber(height) }
	end
end

local function newFormatList(d) 
	local list = {}
	list._undefined = { width = 0, height = 0 }
	local mt = {
		__index = function(t, k)
			return t._undefined
		end
	}
	base.setmetatable(list, mt)
	serializeFormatList(list, d)
	return list
end

local FormatList = { new = newFormatList }

-- VideoInfo class

local function serializeVideoInfo(i, d)
	local params, param = split(d, "&")
	for _, param in base.ipairs(params) do
		local key, value
		_, _, key, value = string.find(param, "(.-)=(.*)")
		base.assert(key and value, "assertion failed: unknown video info parameter format " .. param)
		i[key] = value
	end
end

local function dumpVideoInfo(self)
	for key, value in kpairs(self) do
		if base.type(value) ~= "function" and not string.match(key, "^_") then
			base.print(key .. " = " .. value)
		end
	end
end

local function newVideoInfo(d)
	local info = {}
	info._default = {
		length_seconds = 0,
		author = "(unknown)",
		view_count = 0,
		avg_rating = 0
	}
	info.dump = dumpVideoInfo
	local mt = {
		__newindex = function(t, k, v)
			v = url.unescape(v)
			if k == "title" or k == "author" then
				v = string.gsub(v, "%+", " ")
			end
			base.rawset(t, k, v)
		end,
		__index = function(t, k)
			if t._default[k] then
				return t._default[k]
			else
				base.error("undefined access to video info parameter " .. base.tostring(k), 2)
			end
		end
	}
	base.setmetatable(info, mt)
	if d then
		serializeVideoInfo(info, d)
	end
	return info
end

local VideoInfo = { new = newVideoInfo }

-- PlayerConfig class

local function getPlayerConfig(u)
	local data, error = http.request(u)
	if data then
		local config
		--_, _, config = string.find(data, "yt%.playerConfig%s*=%s*{(.-)}%s*;")
		_, _, config = string.find(data, "ytplayer%.config%s*=%s*{(.-)}%s*;")
		if config then 
			return config
		else
			return nil, "failed to locate yt.playerConfig initialization", data
		end
	else
		return nil, "failed to retrieve resource " .. u .. ": " .. error
	end
end

local function serializePlayerConfig(c, d)
	local i = 1
	while i <= string.len(d) do
		local key
		_, i, key = string.find(d, "\"(.-)\"%s*:%s*", i)
		base.assert(key, "assertion failed: unknown player config format " .. d)
		local value
		if string.find(d, "^{", i + 1) then
			_, i, value = string.find(d, "^{(.-)}%s*,?", i + 1)
			base.assert(value, "assertion failed: unknown player config parameter collection " .. key .. "format " .. d)
			local collection = {}
			base.setmetatable(collection, base.getmetatable(c))
			serializePlayerConfig(collection, value)
			value = collection
		elseif string.find(d, "^\"", i + 1) then
			_, i, value = string.find(d, "^\"([^\"]-)\"%s*,?", i + 1)
		else 
			_, i, value = string.find(d, "([-%w]+)%s*,?", i + 1)
		end
		base.assert(value, "assertion failed: unknown player config parameter " .. key .. "format " .. d)
		c[key] = value
		i = i + 1
	end
end

local function dumpPlayerConfig(self, d)
	d = d or 0
	for key, value in kpairs(self, function(x, y) return base.type(self[x]) == base.type(self[y]) and x < y or base.type(self[x]) ~= "table" and base.type(self[y]) == "table" end) do
		if base.type(value) ~= "function" and not string.match(key, "^_") then
			local indent = ""
			for i = 1, d do
				indent = indent .. "   "
			end
			if (base.type(value) == "table") then
				if base.type(key) ~= "number" then
					base.print(indent .. key .. " = {")
				else
					base.print(indent .. "{")
				end
				d = d + 1
				dumpPlayerConfig(value, d)
				d = d - 1
				base.print(indent .. "}")
			else
				if base.type(key) ~= "number" then
					base.print(indent .. key .. " = " .. value)
				else
					base.print(indent .. value)
				end
			end
		end
	end
end

local function sortStreamMap(self)
	table.sort(self.args.url_encoded_fmt_stream_map, function(x, y)
		if quality[x.quality] > quality[y.quality] then
			return true
		elseif quality[x.quality] < quality[y.quality] then
			return false
		elseif self.args.fmt_list[x.itag].width * self.args.fmt_list[x.itag].height > self.args.fmt_list[y.itag].width * self.args.fmt_list[y.itag].height then
			return true
		elseif self.args.fmt_list[x.itag].width * self.args.fmt_list[x.itag].height < self.args.fmt_list[y.itag].width * self.args.fmt_list[y.itag].height then
			return false
		else
			return x.itag < y.itag
		end
	end)
end

local function selectStream(self, f, q)
	f = f or format._ANY
	q = q or quality._BEST
	if q == quality._BEST then
		q = self.args.url_encoded_fmt_stream_map:getBestQuality()
	end
	self.args.url_encoded_fmt_stream_map:filter("type", f, function(p, f) return f == format._ANY or p.mime == f.mime end)
	self.args.url_encoded_fmt_stream_map:filter("quality", q, function(p, q) return quality[p] >= q end)
	self:sortStreamMap()
	local stream
	for s in self.args.url_encoded_fmt_stream_map:iterate() do
		if not stream or q == quality._LOWEST or q == quality[s.quality] and q ~= quality[stream.quality] then
			stream = s
			if q == quality._BEST then
				break
			end
		end
	end
	return stream
end

local function getVideoInfo(self)
	local resource = "http://www.youtube.com/get_video_info?video_id=" .. self.args.video_id
	local data, error = http.request(resource)
	if (data) then
		return VideoInfo.new(data)
	else
		return VideoInfo.new(data), "failed to retrieve resource " .. resource .. ": " .. error
	end
end

local function newPlayerConfig(u) 
	local config = {}
	config.dump = dumpPlayerConfig
	config.sortStreamMap = sortStreamMap
	config.selectStream = selectStream
	config.getVideoInfo = getVideoInfo
	local mt = {
		__newindex = function(t, k, v)
			if base.type(v) == "string" then
				v = string.gsub(v, "\\u0026", "&")
				v = string.gsub(v, "\\/", "/")	
				if k == "url_encoded_fmt_stream_map" then
					v = StreamMap.new(v)
				elseif k == "fmt_list" then
					v = FormatList.new(v)
				end
			end
			base.rawset(t, k, v)
		end,
		__index = function(t, k)
			base.error("undefined access to player config parameter " .. base.tostring(k), 2)
		end
	}
	base.setmetatable(config, mt)
	local data, error = getPlayerConfig(u)
	if not data then
		return nil, error
	end
	serializePlayerConfig(config, data)
	return config
end

local PlayerConfig = { new = newPlayerConfig }

-- plublic API

function verify(u)
	return string.match(u, "^https?://[w]*%.youtube.-%.com") or string.match(u, "^[w]*%.youtube.-%.com") or string.match(u, "^https?://youtu%.be") or string.match(u, "^youtu%.be")
end

function download(u, f, q)
	local error
	if not verify(u) then
		error = "invalid URL"
	else
		local playerConfig
		playerConfig, error = PlayerConfig.new(u)
		if playerConfig then
			if verbose then
				base.print("[PlayerConfig]\n")
				playerConfig:dump()
				base.print()
				base.print("[StreamMap]\n")
				playerConfig.args.url_encoded_fmt_stream_map:dump()
				base.print()
			end
			local stream = playerConfig:selectStream(f, q)
			if verbose then
				base.print("[Stream]\n")
				stream:dump()
				base.print()
			end
			local data
			local start = os.time()
			data, error = stream:getStream()
			local done = os.time()
			if data then
				local videoInfo
				videoInfo, error = playerConfig:getVideoInfo()
				if verbose then
					base.print("[VideoInfo]\n")
					if not error then
						videoInfo:dump()
					else
						base.print(error)
					end
					base.print()
				end
				return data, nil, stream.type:getFormat().extension, os.difftime(done, start), playerConfig.args.video_id, playerConfig.args.title,
					videoInfo.author, videoInfo.length_seconds, videoInfo.view_count, videoInfo.avg_rating
		end
	end
	end
	return nil, "failed to download stream from " .. u .. ": " .. error
end

function test(u)
	base.print("downloading " .. url.unescape(u))
	u = u or "http://www.youtube.com/watch?v=barWV7RWkq0"
	local data, error, extension, time, id, title, author, length, views, rating = download(u, format._MP4, quality._BEST)
	if data then
		local file = io.open(title .. extension, "wb")
		if file then
			file:write(data)
			file:close()
			base.print("["..u.."] "..title.." ("..length.." sec) uploaded by "..author.." with "..views.." views rated "..rating)
			base.print("downloaded in " .. time .. " seconds")
		end
	else
		base.print("failed to download requested video at ".. u .. ": " .. error)
	end
end
