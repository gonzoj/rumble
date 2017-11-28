local yt = require("youtube")

greet = {
	onLoad = function()
		Rumble.Console.print("using " .. yt._VERSION .. "\n")
	end,
	onExit = function()
		Rumble.Console.print("exiting" .. "\n")
	end
}

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

function onCommandMessage(u, c)
	if not User.getPrivilege(u) == User.Privilege.Authenticated then
		User.sendTextMessage(u, "you have to be registered to use this feature")
		return
	end
	c = unescape(c)
	local full = c
	c = split(c, " ")
	if c[1] == "new" and c[2] then

	elseif c[1] == "set" then

	else
		User.sendTextMessage(u, "invalid command " .. full)
		User.sendTextMessage(u, escape("try 'set'"))
	end
end
