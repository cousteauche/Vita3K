add_library(
	compat
	STATIC
	include/compat/functions.h
	include/compat/state.h
	src/compat.cpp
)

target_include_directories(compat PUBLIC include)

# GUI library - conditional
if(NOT VITA3K_NO_GUI)
	target_link_libraries(compat PUBLIC gui emuenv)
else()
	target_link_libraries(compat PUBLIC emuenv)
endif()

target_link_libraries(compat PRIVATE pugixml::pugixml miniz)
