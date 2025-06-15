set(SOURCE_LIST
	include/kernel/state.h
	include/kernel/types.h
	include/kernel/thread/thread_data_queue.h
	include/kernel/thread/thread_state.h
	include/kernel/thread/host_thread_scheduler.h
	include/kernel/cpu_protocol.h
	include/kernel/sync_primitives.h
	include/kernel/relocation.h
	include/kernel/object_store.h
	include/kernel/debugger.h
	include/kernel/load_self.h
	include/kernel/callback.h
	src/kernel.cpp
	src/thread.cpp
	src/debugger.cpp
	src/load_self.cpp
	src/cpu_protocol.cpp
	src/sync_primitives.cpp
	src/relocation.cpp
	src/callback.cpp
	src/host_thread_scheduler_common.cpp
)

# Add platform-specific scheduler files
if(UNIX AND NOT APPLE)  # Linux
    list(APPEND SOURCE_LIST
        src/host_thread_scheduler_linux.cpp
    )
elseif(WIN32)  # Windows (future support)
    list(APPEND SOURCE_LIST
        src/host_thread_scheduler_windows.cpp
    )
endif()

add_library(
	kernel
	STATIC
	${SOURCE_LIST}
)

target_include_directories(kernel PUBLIC include)
target_link_libraries(kernel PUBLIC rtc cpu mem util nids)
target_link_libraries(kernel PRIVATE patch sdl2 miniz vita-toolchain)

# Host Thread Scheduler definitions
if(UNIX AND NOT APPLE)  # Linux
    target_compile_definitions(kernel PUBLIC VITA3K_HAS_HOST_SCHEDULER=1)
    target_compile_definitions(kernel PUBLIC VITA3K_HAS_LINUX_SCHEDULER=1)  # Keep for compatibility
    
    # Linux-specific dependencies
    find_package(Threads REQUIRED)
    target_link_libraries(kernel PRIVATE Threads::Threads)
    target_compile_definitions(kernel PRIVATE _GNU_SOURCE)  # For CPU_SET macros
    
elseif(WIN32)  # Windows (future support)
    target_compile_definitions(kernel PUBLIC VITA3K_HAS_HOST_SCHEDULER=1)
    target_link_libraries(kernel PRIVATE winmm)  # For timeBeginPeriod/timeEndPeriod
endif()

if(TRACY_ENABLE_ON_CORE_COMPONENTS)
	target_link_libraries(kernel PRIVATE tracy)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})

# Optional: Performance build flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(UNIX AND NOT APPLE)
        target_compile_options(kernel PRIVATE -O3 -march=native)
    elseif(WIN32 AND MSVC)
        target_compile_options(kernel PRIVATE /O2 /arch:AVX2)
    endif()
endif()