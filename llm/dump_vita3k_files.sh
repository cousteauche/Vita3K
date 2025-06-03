#!/bin/bash

# Vita3K VBlank Watchdog - File Content Dumper
# This script dumps all files we need to modify into a single text file
# Expanded to include Graphics, Timing, Sound Subsystem, and Frame Pacing Context
# Paths adjusted for script located in llm/ and executed from Vita3K/ root,
# with output to llm/output/

# Define the output directory relative to the *root directory where the script is executed from*
OUTPUT_DIR="llm/output"
OUTPUT_FILE="${OUTPUT_DIR}/vita3k_vblank_files.txt"

# Create the output directory if it doesn't exist
mkdir -p "$(dirname "$OUTPUT_FILE")"

# Clear/create output file
> "$OUTPUT_FILE"

echo "=== Vita3K VBlank Watchdog File Dump ===" >> "$OUTPUT_FILE"
echo "Generated on: $(date)" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Function to dump file with header
dump_file() {
    local filepath=$1
    echo "===================================================" >> "$OUTPUT_FILE"
    echo "FILE: $filepath" >> "$OUTPUT_FILE"
    echo "===================================================" >> "$OUTPUT_FILE"
    # File existence check: filepath will be relative to the root (e.g., "vita3k/config/include/config/state.h")
    if [ -f "$filepath" ]; then
        cat "$filepath" >> "$OUTPUT_FILE"
    else
        echo "[FILE NOT FOUND] - $filepath" >> "$OUTPUT_FILE" # Added filepath to error message
    fi
    echo "" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"
}

# Base directory for Vita3K source code, relative to the *root directory where the script is executed from*
VITA3K_SRC="vita3k"

# Dump all files we need to modify and for context
echo "Dumping core configuration and state files..."
dump_file "${VITA3K_SRC}/config/include/config/state.h"
dump_file "${VITA3K_SRC}/config/src/config.cpp"
dump_file "${VITA3K_SRC}/emuenv/include/emuenv/state.h"
dump_file "${VITA3K_SRC}/emuenv/src/state.cpp"

echo "Dumping display and main rendering orchestration files..."
dump_file "${VITA3K_SRC}/modules/SceDisplay/SceDisplay.cpp"
dump_file "${VITA3K_SRC}/modules/SceDisplay/SceDisplay.h"
dump_file "${VITA3K_SRC}/display/functions.h"
dump_file "${VITA3K_SRC}/display/src/functions.cpp"
dump_file "${VITA3K_SRC}/display/state.h"

echo "Dumping Vulkan Renderer files..."
dump_file "${VITA3K_SRC}/renderer/src/driver_functions.cpp"
dump_file "${VITA3K_SRC}/renderer/src/vulkan/renderer.cpp"
dump_file "${VITA3K_SRC}/renderer/src/vulkan/surface_cache.cpp"
dump_file "${VITA3K_SRC}/renderer/src/vulkan/pipeline_cache.cpp"
dump_file "${VITA3K_SRC}/renderer/src/vulkan/texture_cache.cpp"
dump_file "${VITA3K_SRC}/renderer/functions.h"
dump_file "${VITA3K_SRC}/renderer/types.h"

echo "Dumping OpenGL Renderer files (for completeness/comparison if needed)..."
dump_file "${VITA3K_SRC}/renderer/src/gl/renderer.cpp"
dump_file "${VITA3K_SRC}/renderer/src/gl/texture_cache.cpp"

echo "Dumping general renderer texture cache files..."
dump_file "${VITA3K_SRC}/renderer/src/texture_cache.cpp"

echo "Dumping kernel thread and timing system files..."
dump_file "${VITA3K_SRC}/kernel/include/kernel/state.h"
dump_file "${VITA3K_SRC}/kernel/include/kernel/time.h"
dump_file "${VITA3K_SRC}/kernel/src/time.cpp"
dump_file "${VITA3K_SRC}/kernel/src/scheduler.cpp"
dump_file "${VITA3K_SRC}/kernel/src/thread/thread.cpp"
dump_file "${VITA3K_SRC}/kernel/src/thread/thread_state.cpp"
dump_file "${VITA3K_SRC}/kernel/include/kernel/thread/thread_state.h"
dump_file "${VITA3K_SRC}/kernel/include/kernel/thread/thread_functions.h"
dump_file "${VITA3K_SRC}/modules/SceKernelThreadMgr/SceThreadmgr.cpp"
dump_file "${VITA3K_SRC}/modules/SceKernelThreadMgr/SceThreadmgrCoredump.cpp"
dump_file "${VITA3K_SRC}/modules/SceFiber/SceFiber.cpp"

echo "Dumping kernel synchronization primitives..."
dump_file "${VITA3K_SRC}/kernel/src/sync/mutex.cpp"
dump_file "${VITA3K_SRC}/kernel/src/sync/semaphore.cpp"
dump_file "${VITA3K_SRC}/kernel/src/sync/condvar.cpp"

echo "Dumping audio subsystem files..."
dump_file "${VITA3K_SRC}/audio/src/audio.cpp"
dump_file "${VITA3K_SRC}/audio/src/ngs.cpp"
dump_file "${VITA3K_SRC}/modules/SceAudio/SceAudio.cpp"
dump_file "${VITA3K_SRC}/modules/SceNgs/SceNgs.cpp"

echo "Dumping shader compilation/cache files..."
dump_file "${VITA3K_SRC}/shader/src/spirv_recompiler.cpp"
dump_file "${VITA3K_SRC}/shader/src/shader_cache.cpp"

echo "Dumping GUI related files (for settings and performance overlay)..."
dump_file "${VITA3K_SRC}/gui/src/settings_dialog.cpp"
dump_file "${VITA3K_SRC}/gui/src/performance_overlay.cpp"

echo "Dumping host integration files..."
dump_file "${VITA3K_SRC}/host/src/host.cpp"
dump_file "${VITA3K_SRC}/host/src/host_func.cpp"

echo "Dumping other relevant modules (IO, Network)..."
dump_file "${VITA3K_SRC}/io/src/io.cpp"
dump_file "${VITA3K_SRC}/io/src/vfs.cpp"
dump_file "${VITA3K_SRC}/modules/SceIofilemgr/SceIofilemgr.cpp"
dump_file "${VITA3K_SRC}/modules/SceNet/SceNet.cpp"
dump_file "${VITA3K_SRC}/modules/SceNetCtl/SceNetCtl.cpp"
dump_file "${VITA3K_SRC}/modules/SceHttp/SceHttp.cpp"

echo "Dumping main application entry/exit points (if they exist)..."
dump_file "${VITA3K_SRC}/app/src/app.cpp"
dump_file "${VITA3K_SRC}/app/src/app_init.cpp"
dump_file "${VITA3K_SRC}/app/src/app_exit.cpp"

echo "Dumping module/game-specific cleanup files..."
dump_file "${VITA3K_SRC}/modules/module_parent.cpp"
dump_file "${VITA3K_SRC}/kernel/src/load_self.cpp"

echo "Dumping CMakeLists files..."
dump_file "${VITA3K_SRC}/kernel/CMakeLists.txt"
dump_file "${VITA3K_SRC}/gui/CMakeLists.txt"
dump_file "${VITA3K_SRC}/modules/CMakeLists.txt"
dump_file "${VITA3K_SRC}/renderer/CMakeLists.txt"
dump_file "${VITA3K_SRC}/audio/CMakeLists.txt"

echo "✅ File dump complete! Check ${OUTPUT_FILE}"
echo "📊 Total size: $(wc -c < "${OUTPUT_FILE}") bytes"
echo "📄 Total lines: $(wc -l < "${OUTPUT_FILE}") lines"