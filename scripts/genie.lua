--
-- Copyright 2025 Milos Tosic. All rights reserved.
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

newoption { trigger = "zidar-path", description = "Path to zidar" }

if not _OPTIONS["zidar-path"] then
	if os.isfile("../../zidar/zidar.lua") then
		_OPTIONS["zidar-path"] = "../../zidar"
	end
	if os.isfile("../zidar/zidar.lua") then
		_OPTIONS["zidar-path"] = "../zidar"
	end
end

dofile(_OPTIONS["zidar-path"] .. "/zidar.lua")
dofile "rprof.lua"

solution "rprof"
	addLibProjects("rprof")
