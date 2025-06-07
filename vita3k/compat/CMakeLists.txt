add_library(
	compat
	STATIC
	include/compat/functions.h
	include/compat/state.h
	src/compat.cpp
)
target_include_directories(compat PUBLIC include)

# VITA3K_NO_GUI: Conditional linking to gui or corey
if(VITA3K_USE_COREY)
    target_link_libraries(compat PUBLIC corey emuenv)
else()
    target_link_libraries(compat PUBLIC gui emuenv)
endif()
target_link_libraries(compat PRIVATE pugixml::pugixml miniz)
