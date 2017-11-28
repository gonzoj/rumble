ip = {
	onLoad = function()
		Rumble.Console.print("starting up..." .. "\n")
	end,
	onExit = function()
		Rumble.Console.print("exiting" .. "\n")
	end
}

function onCommandMessage(u, c)
	if not User.getPrivilege(u) == User.Privilege.Authenticated then
		User.sendTextMessage(u, "you have to be registered to use this feature")
		return
	end
	if not User.getByName(c) then
		User.sendTextMessage(u, "user '" .. c .. "' not found")
	else
		User.requestStats(User.getByName(c))
	end
end

function onUserStats(u)
	if User.getAddress(u) then
		Channel.sendTextMessage(User.getChannel(u), "user '" .. User.getName(u) .. "' has address " .. User.getAddress(u))
	else
		Channel.sendTextMessage(User.getChannel(u), "user '" .. User.getName(u) .. "' has address 0.0.0.0")
	end
end
