add_library(
	lang
	STATIC
	include/lang/state.h
	include/lang/functions.h
	src/lang.cpp
)
target_include_directories(lang PUBLIC include)
target_link_libraries(lang PUBLIC emuenv compat ime)

# VITA3K_NO_GUI: Conditional linking to gui or corey
if(VITA3K_USE_COREY)
    target_link_libraries(lang PRIVATE config corey pugixml::pugixml util)
else()
    target_link_libraries(lang PRIVATE config gui pugixml::pugixml util)
endif()
