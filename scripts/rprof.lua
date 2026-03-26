--
-- Copyright 2025 Milos Tosic. All rights reserved.
-- License: http://www.opensource.org/licenses/BSD-2-Clause
--

function projectExtraConfig_rprof() 
	includedirs	{ path.join(projectGetPath("rprof"), "../") }
end

function projectExtraConfigExecutable_rprof() 
	includedirs	{ path.join(projectGetPath("rprof"), "../") }
end

function projectAdd_rprof() 
	addProject_lib("rprof")
end

