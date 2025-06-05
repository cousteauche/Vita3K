# File: vita3k/dialog/CMakeLists.txt
# VITA3K_NO_GUI: Modified for GUI-free build

add_library(
    dialog
    INTERFACE
)

target_include_directories(dialog INTERFACE include)
# VITA3K_NO_GUI: Add lang dependency since we use DialogLangState from lang/state.h
target_link_libraries(dialog INTERFACE lang io)
