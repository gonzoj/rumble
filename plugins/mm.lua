local E = 1250

local score = {
	WIN = 1,
	LOSS = 0,
	DRAW = 0.5
}

local function K(x)
	if x > 10 then
		return 32
	else
		return 100 - math.floor(((100 - 32) / 10) * (x - 1))
	end
end

local function kpairs(t, o)
	local keys = {}
	for k in pairs(t) do
		table.insert(keys, k)
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

local function copy(t, i)
	i = i or ipairs
	local c = {}
	for k, v in i(t) do
		c[k] = v
	end
	return c
end

local function concat(a, b, i)
	i = i or ipairs
	for k, v in i(b) do
		if type(k) == "number" then
			table.insert(a, v)
		else
			a[k] = v
		end
	end
	return a
end

local function average(t, k)
	local s = 0
	for _, v in ipairs(t) do
		if k then
			s = s + v[k]
		else
			s = s + v
		end
	end
	return s / #t
end

local function norm(p, t, k)
	assert(p >= 1, "norm: p >= 1")
	local s = 0
	for _, v in ipairs(t) do
		if k then
			s = s + math.abs(v[k])^p
		else
			s = s + math.abs(v)^p
		end
	end
	s = s^(1 / p)
	return s
end

local function effective(t, p)
	if p then
		return norm(2, t, "rating") * (p.rating / average(t, "rating"))
	else
		return norm(2, t, "rating")
	end
end

local function fact(n)
	assert(n >= 0, "factorial: n >= 0)")
	local r = 1
	for i = 2, n do
		r = r * i
	end
	return r
end

local function bincoff(n, k)
	assert(k > 0 and n >= k, "binomial coefficient: 0 < k <= n")
	return fact(n) / (fact(k) * fact(n - k))
end

local function permutations(a, k)
	local n = #a
	local x = {}
	local y = {}
	local o = {}
	for i = 1, k do
		o[i] = i
	end
	local r = bincoff(n, k)
	for i = 1, r do
		x[i] = {}
		y[i] = {}
		local c = copy(a)
		for j = 1, k do
			x[i][j] = a[o[j]]
			c[o[j]] = nil
		end
		for j = 1, n do
			if c[j] then
				table.insert(y[i], c[j])
			end
		end
		for u = k, 1, -1 do
			o[u] = (o[u] + 1) % (n - (k - u) + 1)
			for v = u + 1, k do
				o[v] = o[v - 1] + 1
			end
			if o[u] > 0 then
				break
			end
		end
	end
	return x, y, r
end

local function balance(t)
	local k = math.floor(#t / 2)
	local a, b, n = permutations(t, k)
	local r
	local o
	for i = 1, n do
		d = math.abs(effective(a[i]) - effective(b[i]))
		if not o or d < o then
			o = d
			r = i
		end
	end
	if effective(a[r]) < effective(b[r]) then
		return a[r], b[r]
	elseif effective(b[r]) < effective(a[r]) then
		return b[r], a[r]
	else
		if average(a[r], "rating") < average(b[r], "rating") then
			return a[r], b[r]
		else
			return b[r], a[r]
		end
	end
end

local function color(s, c)
	return string.format("<span style=\"color:#%s\">%s</span>", c, s)
end

local function bold(s)
	return string.format("<b>%s</b>", s)
end

local function division(p)
	if p.rating < 1200 then
		return "8b4513" -- brown
	elseif p.rating < 1300 then
		return "cd661d" -- bronze
	elseif p.rating < 1400 then
		return "b8b8b8" -- silver
	elseif p.rating < 1500 then
		return "ffd700" -- gold
	else
		return "00bfff" -- platinum
	end
end

local player = {
	new = function(n)
		local p = { name = n, rating = E, wins = 0, losses = 0, draws = 0 }
		p.total = 0
		p.ratio = 0
		p.reset = function(self)
			self.rating = E
			self.wins = 0
			self.losses = 0
			self.draws = 0
			self.total = 0
			self.ratio = 0
		end
		p.copy = function(self)
			local c = { name = self.name }
			c.rating = self.rating
			c.wins = self.wins
			c.losses = self.losses
			c.draws = self.draws
			c.total = self.total
			c.ratio = self.ratio
			return c
		end
		p.update = function(self, own, opp, s)
			self.total = self.total + 1
			if s == score.WIN then
				self.wins = self.wins + 1
			elseif s == score.LOSS then
				self.losses = self.losses + 1
			else
				self.draws = self.draws + 1
			end
			self.ratio = math.floor((self.wins * 100) / self.total)
			Rumble.Console.print(string.format("player %s updating elo with opposite effective elo %i and own effective elo %i", self.name, effective(opp), effective(own, self)))
			self.rating = self.rating + math.floor(K(self.total) * (s - (1 / (1 + 10^((effective(opp) - effective(own, self)) / 400)))))
		end
		p.save = function(self, db)
			db:write(string.format("%s\t%s\t%s\t%s\t%s\n", self.name, self.rating, self.wins, self.losses, self.draws))
		end
		p.load = function(self, s)
			s = split(s, "\t")
			self.name = s[1]
			self.rating = tonumber(s[2])
			self.wins = tonumber(s[3])
			self.losses = tonumber(s[4])
			self.draws = tonumber(s[5])
			self.total = self.wins + self.losses + self.draws
			if self.total > 0 then
				self.ratio = math.floor((self.wins * 100) / self.total)
			else
				self.ratio = 0
			end
		end
		setmetatable(p, {
			__tostring = function(self)
				return string.format("%s: %s [%s - %s - %s] %i%%", self.name, color(bold(self.rating), division(self)), color(self.wins, "00ff00"), color(self.losses, "ff0000"), color(self.draws, "ffcc00"), self.ratio)
			end
		})
		return p
	end
}

local function compare_teams(a, b)
	if #a ~= #b then
		return false
	end
	for _, x in ipairs(a) do
		local found = false
		for _, y in ipairs(b) do
			if x.name == y.name then
				found = true
				break
			end
		end
		if not found then
			return false
		end
	end
	return true
end

local game = {
	new = function(ts, s)
		ts = ts or os.time()
		s = s or -1
		local g = { team = {}, timestamp = ts, score = s }
		g.add = function(self, t)
			table.insert(self.team, t)
		end
		g.equal = function(self, o)
			if #self.team ~= #o.team then
				return false
			else
				for i in ipairs(self.team) do
					local found = false
					for j in ipairs(o.team) do
						if compare_teams(self.team[i], o.team[j]) then
							found = true
							break
						end
					end
					if not found then
						return false
					end
				end
				return true
			end
		end
		g.update = function(self, s)
			self.score = s or self.score
			if self.score >= 0 then
				local copy = {}
				for _, t in ipairs(self.team) do
					local c = {}
					for _, p in ipairs(t) do
						table.insert(c, p:copy())
					end
					table.insert(copy, c)
				end
				for i, t in ipairs(self.team) do
					local opp = {}
					for j in ipairs(self.team) do
						if j ~= i then
							opp = concat(opp, copy[j])
						end
					end
					s = score.WIN
					if self.score ~= i then
						if self.score == 0 then
							s = score.DRAW
						else
							s = score.LOSS
						end
					end
					for _, p in ipairs(t) do
						p:update(copy[i], opp, s)
					end
				end
			end
		end
		g.save = function(self, db)
			db:write(string.format("%i\t%i\t", self.timestamp, self.score))
			for _, t in ipairs(self.team) do
				for _, p in ipairs(t) do
					db:write(string.format("\t%s", p.name))
				end
				db:write("\t")
			end
			db:write("\n")
		end
		g.load = function(self, l, s)
			local t = split(s, "\t\t")
			local i = split(t[1], "\t")
			self.timestamp = tonumber(i[1])
			self.score = tonumber(i[2])
			for i = 2, #t do
				local players = split(t[i], "\t")
				local team = {}
				for _, n in ipairs(players) do
					table.insert(team, l.players[n])
				end
				table.insert(self.team, team)
			end
		end
		g.contains = function(self, p)
			for i, t in ipairs(self.team) do
				for _, _p in ipairs(t) do
					if _p.name == p.name then
						if self.score == i then
							return score.WIN
						elseif self.score > 0 then
							return score.LOSS
						elseif self.score == 0 then
							return score.DRAW
						else
							return -1
						end
					end
				end
			end
			return -2
		end
		setmetatable(g, {
			__tostring = function(self)
				local s = os.date("[%F %T", self.timestamp) .. "] "
				for i, t in ipairs(self.team) do
					if i > 1 then
						s = s .. bold(" vs ")
					end
					for j, p in ipairs(t) do
						if j > 1 then
							s = s .. ", "
						end
						local c
						if self.score == i then
							c = "00ff00"
						elseif self.score == 0 then
							c = "ffcc00"
						elseif self.score > 0 then
							c = "ff0000"
						end
						if c then
							s = s .. color(p.name, c)
						else
							s = s .. p.name
						end
					end
				end
				return s
			end
		})
		return g
	end
}

local function blocked(l, p)
	for _, n in ipairs(l) do
		if n == p.name then
			return true
		end
	end
	return false
end

local function stats(self, p, b)
	if p.total == 0 then
		return ""
	end
	local c = nil
	if b then
		c = copy(b)
		table.remove(c, 1)
	end
	local streak = 0
	local type
	for i = table.maxn(self.games), 1, -1 do
		local game = self.games[i]
		local s = game:contains(p) 
		if s >= 0 then
			if streak == 0 then
				streak = 1
				type = s
			else 
				if type == s then
					streak = streak + 1
				else
					break
				end
			end
		end
	end
	local winstreak = 0
	local lossstreak = 0
	local ws = 0
	local ls = 0
	for _, game in ipairs(self.games) do
		local s = game:contains(p)
		if s >= 0 then
			if s == score.WIN then
				ws = ws + 1
				ls = 0
			elseif s == score.LOSS then
				ws = 0
				ls = ls + 1
			else
				ws = 0
				ls = 0
			end
		end
		if ws > winstreak then
			winstreak = ws
		end
		if ls > lossstreak then
			lossstreak = ls
		end
	end
	local players = {}
	for _, _p in self:iterate() do
		if p.name ~= _p.name and c and not blocked(c, _p) then
			local player = { p = _p, with = { win = 0, loss = 0, draw = 0, games = 0 }, vs = { win = 0, loss = 0, draw = 0, games = 0 } }
			player.vs.ratio = { win = 0, loss = 0, draw = 0 }
			player.with.ratio = { win = 0, loss = 0, draw = 0 }
			for _, game in ipairs(self.games) do
				local a = game:contains(p)
				local b = game:contains(_p)
				if a >= 0 and b >= 0 then
					if a == b then
						player.with.games = player.with.games + 1
						if a == score.WIN then
							player.with.win = player.with.win + 1
						elseif a == score.LOSS then
							player.with.loss = player.with.loss + 1
						else
							player.with.draw = player.with.draw + 1
						end
					else
						player.vs.games = player.vs.games + 1
						if a == score.WIN then
							player.vs.win = player.vs.win + 1
						elseif a == score.LOSS then
							player.vs.loss = player.vs.loss + 1
						else
							player.vs.draw = player.vs.draw + 1
						end
					end
				end
			end
			if player.with.games > 0 then
				player.with.ratio = { win = math.floor((player.with.win * 100) / player.with.games),
					loss = math.floor((player.with.loss * 100) / player.with.games),
					draw = math.floor((player.with.draw * 100) / player.with.games)
				}
			end
			if player.vs.games > 0 then
				player.vs.ratio = { win = math.floor((player.vs.win * 100) / player.vs.games),
					loss = math.floor((player.vs.loss * 100) / player.vs.games),
					draw = math.floor((player.vs.draw * 100) / player.vs.games)
				}
			end
			table.insert(players, player)
		end
	end
	local s = bold(p.name) .. ": "
	if type == score.WIN then
		s = s .. color(streak .. "-win streak", "00ff00")
	elseif type == score.LOSS then
		s = s .. color(streak .. "-loss streak", "ff0000")
	else
		s = s .. color(streak .. "-draw streak", "ffcc00")
	end
	s = s .. "<br>\n"
	s = s .. "longest winning streak: " .. color(winstreak, "00ff00") .. "<br>\n"
	s = s .. "longest losing streak: " .. color(lossstreak, "ff0000") .. "<br>\n"
	table.sort(players, function(a, b)
		if a.with.ratio.win == b.with.ratio.win then
			if a.with.games == b.with.games then
				return a.p.name < b.p.name
			else
				return a.with.games > b.with.games
			end
		else
			return a.with.ratio.win > b.with.ratio.win
		end
	end)
	if players[1].with.games > 0 then
		s = s .. string.format("best performance with: %s [%s - %s - %s] %i%% (%i)", color(players[1].p.name , "00ff00"), color(players[1].with.win, "00ff00"), color(players[1].with.draw, "ffcc00"), color(players[1].with.loss, "ff0000"), players[1].with.ratio.win, players[1].with.games) .. "<br>\n"
	end
	table.sort(players, function(a, b)
		if a.with.ratio.loss == b.with.ratio.loss then
			if a.with.games == b.with.games then
				return a.p.name < b.p.name
			else
				return a.with.games > b.with.games
			end
		else
			return a.with.ratio.loss > b.with.ratio.loss
		end
	end)
	if players[1].with.games > 0 then
		s = s .. string.format("worst performance with: %s [%s - %s - %s] %i%% (%i)", color(players[1].p.name , "ff0000"), color(players[1].with.win, "00ff00"), color(players[1].with.draw, "ffcc00"), color(players[1].with.loss, "ff0000"), players[1].with.ratio.win, players[1].with.games) .. "<br>\n"
	end
	table.sort(players, function(a, b)
		if a.vs.ratio.win == b.vs.ratio.win then
			if a.vs.games == b.vs.games then
				return a.p.name < b.p.name
			else
				return a.vs.games > b.vs.games
			end
		else
			return a.vs.ratio.win > b.vs.ratio.win
		end
	end)
	if players[1].vs.games > 0 then
		s = s .. string.format("best performance versus: %s [%s - %s - %s] %i%% (%i)", color(players[1].p.name , "00ff00"), color(players[1].vs.win, "00ff00"), color(players[1].vs.draw, "ffcc00"), color(players[1].vs.loss, "ff0000"), players[1].vs.ratio.win, players[1].vs.games) .. "<br>\n"
	end
	table.sort(players, function(a, b)
		if a.vs.ratio.loss == b.vs.ratio.loss then
			if a.vs.games == b.vs.games then
				return a.p.name < b.p.name
			else
				return a.vs.games > b.vs.games
			end
		else
			return a.vs.ratio.loss > b.vs.ratio.loss
		end
	end)
	if players[1].vs.games > 0 then
		s = s .. string.format("worst performance versus: %s [%s - %s - %s] %i%% (%i)", color(players[1].p.name , "ff0000"), color(players[1].vs.win, "00ff00"), color(players[1].vs.draw, "ffcc00"), color(players[1].vs.loss, "ff0000"), players[1].vs.ratio.win, players[1].vs.games) .. "<br>\n"
	end
	table.sort(players, function(a, b)
		if a.with.games == b.with.games then
			return a.p.name < b.p.name
		else
			return a.with.games > b.with.games
		end
	end)
	if players[1].with.games > 0 then
		s = s .. string.format("most played with: %s - %i (%i%%)<br>\n", players[1].p.name, players[1].with.games, players[1].with.games  * 100 / p.total)
	end
	table.sort(players, function(a, b)
		if a.vs.games == b.vs.games then
			return a.p.name < b.p.name
		else
			return a.vs.games > b.vs.games
		end
	end)
	if players[1].vs.games > 0 then
		s = s .. string.format("most played versus: %s - %i (%i%%)<br>\n", players[1].p.name, players[1].vs.games, players[1].vs.games  * 100 / p.total)
	end
	return s
end

local ladder = {
	new = function(n)
		local l = { name = n, size = 0, players = {}, games = {} }
		l.add = function(self, p)
			self.players[p.name] = p
			self.size = self.size + 1
		end
		l.game = function(self, t, ts, s)
			for _, team in ipairs(t) do
				for i, name in ipairs(team) do
					if self:contains(name) then
						team[i] = self.players[name]
					else
						return nil, name
					end
				end
			end
			local g = game.new(ts, s)
			if #t < 2 then
				table.sort(t[1], function(a, b)
					if a.rating == b.rating then
						if a.total == b.total then
							return a.name < b.name
						else
							return a.total > b.total
						end
					else
						return a.rating > b.rating
					end
				end)
				local x, y = balance(t[1])
				table.sort(x, function(a, b) 
					if a.rating == b.rating then
						if a.total == b.total then
							return a.name < b.name
						else
							return a.total > b.total
						end
					else
						return a.rating > b.rating
					end
				end)
				g:add(x)
				table.sort(y, function(a, b) 
					if a.rating == b.rating then
						if a.total == b.total then
							return a.name < b.name
						else
							return a.total > b.total
						end
					else
						return a.rating > b.rating
					end
				end)
				g:add(y)
			else
				for _, team in ipairs(t) do
					g:add(team)
				end
			end
			table.insert(self.games, g)
			table.sort(self.games, function(a, b) return a.timestamp < b.timestamp end)
			return g
		end
		l.cancel = function(self, c)
			for i, g in ipairs(self.games) do
				if g == c then
					table.remove(self.games, i)
					break
				end
			end
		end
		l.iterate = function(self)
			return kpairs(self.players, function(a, b) 
				if self.players[a].rating == self.players[b].rating then
					if self.players[a].total == self.players[b].total then
						return self.players[a].name < self.players[b].name
					else
						return self.players[a].total > self.players[b].total
					end
				else
					return self.players[a].rating > self.players[b].rating
				end
			end)
		end
		l.create = function(self)
			io.open("ladders/" .. self.name .. ".pdb"):close()
			io.open("ladders/" .. self.name .. ".mdb"):close()
		end
		l.update = function(self, g, s)
			g:update(s)
		end
		l.reset = function(self)
			for _, p in self:iterate() do
				p:reset()
			end
		end
		l.reroll = function(self)
			for _, g in ipairs(self.games) do
				g:update()
			end
		end
		l.contains = function(self, a)
			if type(a) == "string" then
				return self.players[a] ~= nil
			elseif type(a) == "table" then
				for _, n in ipairs(a) do
					if not self.players[n] then
						return false, n
					end
				end
				return true
			else
				return false
			end
		end
		l.history = function(self)
			local s = string.format("match history for ladder '%s' (%i matches):<br>\n<br>\n", self.name, #self.games)
			for _, g in ipairs(self.games) do
				s = s .. tostring(g) .. "<br>\n"
			end
			return s
		end
		l.save = function(self)
			local db = io.open("ladders/" .. self.name .. ".pdb", "w")
			for _, p in self:iterate() do
				p:save(db)
			end
			db:close()
			db = io.open("ladders/" .. self.name .. ".mdb", "w")
			for _, g in ipairs(self.games) do
				g:save(db)
			end
			db:close()
		end
		l.load = function(self)
			for line in io.lines("ladders/" .. self.name .. ".pdb") do
				local p = player.new()
				p:load(line)
				self:add(p)
			end
			for line in io.lines("ladders/" .. self.name .. ".mdb") do
				local g = game.new()
				g:load(self, line)
				table.insert(self.games, g)
			end
		end
		l.stats = function(self, c)
			--local s = string.format("statistics for ladder '%s':<br>\n<br>\n", self.name)
			local s = ""
			for _, p in self:iterate() do
				s = s .. "<br>\n"
				s = s .. stats(self, p, c)
			end
			return s
		end
		setmetatable(l, {
			__tostring = function(self)
				local s = string.format("ladder '%s' (%i players):<br>\n<br>\n", self.name, self.size)
				local total = 0
				local i = 1
				for  _, p in self:iterate() do
					s = s .. i .. ". " .. tostring(p) .. "<br>\n"
					i = i + 1
					total = total + p.rating
				end
				local av = { rating = total / self.size }
				s = s .. "<br>\naverage elo: " .. bold(color(av.rating, division(av))) .. "<br>\n"
				return s
			end
		})
		return l
	end
}

local _ladder
local _game

local function link(l)
	return string.format("<a href=\"%s\">%s</a>", string.gsub(l, "&", "&amp;"), l)
end

local function count(s, c)
	local n = 0
	for i = 1, s:len(), 1 do
		if s:byte(i) == c:byte(1) then
			n = n + 1
		end
	end
	return n
end

local function tokenize(h, u, t, m)
	t = t or 10
	local i = 1
	local j = 0
	local n = 1
	local b = false
	local d = 0
	local c = count(h, "\n")
	for l in string.gmatch(h, ".-\n") do
		j = j + string.len(l)
		if n % t == 0 then
			if not m or n >= c - m then
				if b then
					Channel.sendTextMessage(User.getChannel(u), "<br>" .. string.sub(h, i, j))
				else
					Channel.sendTextMessage(User.getChannel(u), string.sub(h, i, j))
				end
			end
			i = j + 1
			b = true
			d = j
		end
		n = n + 1
	end
	if d ~= j then
		if b then
			Channel.sendTextMessage(User.getChannel(u), "<br>" .. string.sub(h, i, j))
		else
			Channel.sendTextMessage(User.getChannel(u), string.sub(h, i, j))
		end
	end
end

local civilizations = {
	"America",
	"Arabia",
	"Aztecs",
	"Babylon",
	"Byzantium",
	"Carthage",
	"Celts",
	"China",
	"Dutch",
	"Egypt",
	"England",
	"Ethiopia",
	"France",
	"Germany",
	"Greece",
	"Holy Roman Empire",
	"Inca",
	"India",
	"Japan",
	"Khmer",
	"Korea",
	"Mali",
	"Maya",
	"Mongolia",
	"Native America",
	"Ottomans",
	"Persia",
	"Portugal",
	"Rome",
	"Russia",
	"Spain",
	"Sumeria",
	"Vikings",
	"Zulu",
}

local leaders = {
	"Alexander",
	"Asoka",
	"Augustus Caesar",
	"Bismarck",
	"Boudica",
	"Brennus",
	"Catherine",
	"Charlemagne",
	"Churchill",
	"Cyrus",
	"Darius I",
	"De Gaulle",
	"Elizabeth",
	"Roosevelt",
	"Frederick",
	"Gandhi",
	"Genghis Khan",
	"Gilgamesh",
	"Hammurabi",
	"Hannibal",
	"Hatshepsut",
	"Huayna Capac",
	"Isabella",
	"Joao II",
	"Julius Caesar",
	"Justinian I",
	"Kublai Khan",
	"Lincoln",
	"Louis XIV",
	"Mansa Musa",
	"Mao Zedong",
	"Mehmed II",
	"Montezuma",
	"Napoleon",
	"Pacal II",
	"Pericles",
	"Peter",
	"Qin Shi Huang",
	"Ragnar",
	"Ramesses II",
	"Saladin",
	"Shaka",
	"Sitting Bull",
	"Stalin",
	"Suleiman",
	"Suryavarman II",
	"Tokugawa",
	"Victoria",
	"Wang Kon",
	"Washington",
	"Willem van Oranje",
	"Zara Yaqob"
}

local function rand_civ4_pick()
	return "random pick of the match: " .. leaders[math.random(#leaders)] .. " of " .. civilizations[math.random(#civilizations)]
end

function onCommandMessage(u, c)
	local cmd = c
	c = split(c, " ")
	if c[1] == "game" then
		if _game then
			User.sendTextMessage(u, "you have to finish or cancel your on-going game first")
			User.sendTextMessage(u, tostring(_ladder.games[_game]))
		else
			c = string.match(cmd, "game (.*)")
			local ts, s
			local _, j, i = string.find(c, "(.-) | ")
			if i then
				local y, m, d, h, min, sec, score = string.match(i, "(%d%d%d%d)%-(%d%d)%-(%d%d) (%d%d):(%d%d):(%d%d) (%d)")
				if not y then
					User.sendTextMessage(u, "nope.")
				else
					local time = { year = tonumber(y), month = tonumber(m), day = tonumber(d), hour = tonumber(h), min = tonumber(min), sec = tonumber(sec), isdst = false }
					ts = os.time(time)
					s = tonumber(score)
					c = string.sub(c, j + 1)
				end
			end
			c = split(c, " vs ")
			local teams = {}
			for _, t in ipairs(c) do
				table.insert(teams, split(t, " "))
			end
			local e
			_game, e = _ladder:game(teams, ts, s)
			if not _game then
				User.sendTextMessage(u, string.format("no player '%s' subscribed to ladder '%s'", e, _ladder.name))
				_game = nil
			else
				if s then
					_ladder:update(_game)
				else
					local m = ""
					local n = 0
					for _, g in ipairs(_ladder.games) do
						if g ~= _game and g.score >= 0 and _game:equal(g) then
							n = n + 1
							m = m .. tostring(g) .. "<br>\n"
						end
					end
					if n > 0 then
						m = string.format("previous games (%i):<br>\n<br>\n%s<br>\n", n, m)
						Channel.sendTextMessage(User.getChannel(u), m)
					end
					m = ""
					n = 0
					for _, g in ipairs(_ladder.games) do
						if g ~= _game and g.score < 0 and _game:equal(g) then
							n = n + 1
							m = m .. tostring(g) .. "<br>\n"
						end
					end
					if n > 0 then
						m = string.format("open games (%i):<br>\n<br>\n%s<br>\n", n, m)
						Channel.sendTextMessage(User.getChannel(u), m)
					end
					m = "player pool:<br>\n<br>\n"
					for _, t in ipairs(_game.team) do
						for _, p in ipairs(t) do
							m = m .. tostring(p) .. "<br>\n"
						end
					end
					Channel.sendTextMessage(User.getChannel(u), m)
				end
				Channel.sendTextMessage(User.getChannel(u), tostring(_game))
				_ladder:save()
			end
		end
	elseif c[1] == "join" then
		if _ladder:contains(User.getName(u)) then
			User.sendTextMessage(u, string.format("you have already subscribed to ladder '%s'", User.getName(u), _ladder.name))
		else
			local p = player.new(User.getName(u))
			_ladder:add(p)
			_ladder:save()
		end
	elseif c[1] == "add" then
		if not c[2] then
			User.sendTextMessage(u, "please specify a player to add")
		elseif _ladder:contains(c[2]) then
			User.sendTextMessage(u, string.format("player '%s' already subscribed to ladder '%s'", c[2], _ladder.name))
		else
			local p = player.new(c[2])
			_ladder:add(p)
			_ladder:save()
		end
	elseif c[1] == "cancel" then
		if not _game then
			User.sendTextMessage(u, "no on-going game found")
		else
			_ladder:cancel(_game)
			_game = nil
		end
	elseif c[1] == "win" then
		if not _game then
			User.sendTextMessage(u, "no on-going game found")
		elseif not c[2] then
			User.sendTextMessage(u, "please specify the winning team")
		else
			local t = tonumber(c[2])
			if not t or t <= 0 or t > #_game.team then
				User.sendTextMessage(u, "nope.")
			else
				_ladder:update(_game, t)
				_ladder:save()
				_game = nil
			end
		end
	elseif c[1] == "draw" then
		if not _game then
			User.sendTextMessage(u, "no on-going game found")
		else
			_ladder:update(_game, 0)
			_ladder:save()
			_game = nil
		end
	elseif c[1] == "pause" then
		if not _game then
			User.sendTextMessage(u, "no on-going game found")
		else
			_game = nil
		end
	elseif c[1] == "ladder" then
		Channel.sendTextMessage(User.getChannel(u), tostring(_ladder))
	elseif c[1] == "history" then
		--Channel.sendTextMessage(User.getChannel(u), _ladder:history())
		if not c[2] then
			tokenize(_ladder:history(), u, nil, 30)
		elseif c[2] == "all" then
			tokenize(_ladder:history(), u)
		else
			local t = tonumber(c[2])
			if not t or t <= 0 then
				User.sendTextMessage(u, "nope.")
			else
				tokenize(_ladder:history(), u, nil, t)
			end
		end
	elseif c[1] == "reset" then
		_ladder:reset()
		_ladder:save()
	elseif c[1] == "reroll" then
		_ladder:reroll()
		_ladder:save()
	elseif c[1] == "links" then
		Channel.sendTextMessage(User.getChannel(u), link("http://www.civfanatics.com/civ4/info/civilizations"))
		Channel.sendTextMessage(User.getChannel(u), link("http://www.civfanatics.com/civ4/info/units/"))
		Channel.sendTextMessage(User.getChannel(u), link("http://www.civfanatics.com/civ4/reference/leader_picker_bts.html"))
		Channel.sendTextMessage(User.getChannel(u), link("http://realmsbeyond.net/forums/showthread.php?tid=7123"))
	elseif c[1] == "switch" then
		if not c[2] then
			User.sendTextMessage(u, "please specify the ladder to switch to")
		else
			Rumble.Console.print("saving ladder...\n")
			_ladder:save()
			Rumble.Console.print("loading ladder...\n")
			_ladder = ladder.new(c[2])
			_ladder:load()
		end
	elseif c[1] == "bans" then
		Channel.sendTextMessage(User.getChannel(u), "<b>default bans:</b><br>rome, maya, mali, aztec, native america, inca")
	elseif c[1] == "map" then
		Channel.sendTextMessage(User.getChannel(u), "<b>settings:</b><br><b>[3v3]</b><br><b>quick:</b> Inland_Sea/LD_Inversed_Inland_Sea, <b>Tiny</b>, Temperate, <b>Low</b>, Ancient, Quick, Flat, Balanced<br><b>slow:</b> Inland_Sea/LD_Inversed_Inland_Sea, <b>Small</b>, Temperate, <b>High</b>, Ancient, Quick, Flat, Balanced<br><b>[2v2]</b><br>Inland_Sea/LD_Inversed_Inland_Sea, Tiny, Temperate, Medium, Ancient, Quick, Flat, Balanced")
	elseif c[1] == "stats" then
		--Channel.sendTextMessage(User.getChannel(u), _ladder:stats())
		Channel.sendTextMessage(User.getChannel(u), "statistics for ladder '" .. _ladder.name .. "':")
		tokenize(_ladder:stats(c), u, 10)
	elseif c[1] == "random" then
		Channel.sendTextMessage(User.getChannel(u), rand_civ4_pick())
	else
		User.sendTextMessage(u, "nope.")
	end
end

mm = {
	onLoad = function()
		Rumble.Console.print("loading ladder...\n")
		_ladder = ladder.new("wc3td")
		--_ladder = ladder.new("civilization4")
		_ladder:load()
		Rumble.Console.print(tostring(_ladder))
		Rumble.Console.print(_ladder:history())
		math.randomseed(os.time())
	end,
	onExit = function()
		Rumble.Console.print("saving ladder...\n")
		_ladder:save()
		Rumble.Console.print(tostring(_ladder))
		Rumble.Console.print(_ladder:history())
	end
}
