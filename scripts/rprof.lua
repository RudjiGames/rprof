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

-- bgfx-enabled variant of the rprof library.
--
-- A sample that ships a "<sample>_uses_bgfx.h" marker links "<lib>_bgfx"
-- instead of the plain library (see zidar/project_lib_sample.lua). The rprof
-- samples are rapp applications that draw the ImGui visualizer (rprof_imgui.h),
-- so the variant pulls in rapp's own bgfx variant - that is what provides the
-- ImGui / bgfx symbols and include paths the sample needs.

function projectDependencies_rprof_bgfx()
	-- rapp_bgfx is a variant of rapp; the dependency resolver can only map
	-- "rapp_bgfx" -> "rapp" once rapp.lua is loaded (it defines the
	-- projectAdd_rapp_bgfx hook used for that lookup). rprof's solution never
	-- loads rapp otherwise, so make sure its script is in before we ask for it.
	projectLoad("rapp", false)
	return { "rapp_bgfx", "bgfx" }
end

function projectExtraConfig_rprof_bgfx()
	includedirs	{ path.join(projectGetPath("rprof"), "../") }
end

function projectExtraConfigExecutable_rprof_bgfx()
	includedirs	{ path.join(projectGetPath("rprof"), "../") }
end

function projectAdd_rprof_bgfx()
	-- Same sources as "rprof", built as "rprof_bgfx" (PCH disabled - rprof has
	-- no PCH header and the variant shares the base source tree).
	addProject_lib("rprof", nil, false, "_bgfx", true)
end
