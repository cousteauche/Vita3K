# File: vita3k/emuenv/CMakeLists.txt
# VITA3K_NO_GUI: Modified for GUI-free build

add_library(
	emuenv
	STATIC
	src/emuenv.cpp)
target_include_directories(emuenv INTERFACE include)
target_link_libraries(emuenv PUBLIC mem)
# VITA3K_NO_GUI: Use lang instead of dialog (lang/state.h has DialogState)
target_link_libraries(emuenv PUBLIC lang)
target_link_libraries(emuenv PRIVATE audio config ctrl dialog display ime io kernel motion net ngs nids np regmgr renderer touch gdbstub packages http)
