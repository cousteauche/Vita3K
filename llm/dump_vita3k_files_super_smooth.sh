#!/bin/bash

# Vita3K Super Smooth Dumper
# This script dumps critical files for frame pacing, graphics, and audio analysis.
# Designed for the latest Vita3K/Vita3K:master upstream branch structure.
# Execute from the Vita3K project root directory.
# Output goes to llm/output/

# Define the output directory relative to the *root directory where the script is executed from*
OUTPUT_DIR="llm/output"
OUTPUT_FILE="${OUTPUT_DIR}/vita3k_super_smooth_context.txt"

# Create the output directory if it doesn't exist
mkdir -p "$(dirname "$OUTPUT_FILE")"

# Clear/create output file
> "$OUTPUT_FILE"

echo "=== Vita3K Super Smooth Context Dump ===" >> "$OUTPUT_FILE"
echo "Generated on: $(date)" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Function to dump file with header
dump_file() {
    local filepath=$1
    echo "===================================================" >> "$OUTPUT_FILE"
    echo "FILE: $filepath" >> "$OUTPUT_FILE"
    echo "===================================================" >> "$OUTPUT_FILE"
    if [ -f "$filepath" ]; then
        cat "$filepath" >> "$OUTPUT_FILE"
    else
        echo "[FILE NOT FOUND] - $filepath" >> "$OUTPUT_FILE"
    fi
    echo "" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"
}

# Base directory for Vita3K source code, relative to the *root directory where the script is executed from*
VITA3K_SRC="vita3k"

echo "Dumping core configuration and environment state..."
dump_file "${VITA3K_SRC}/config/include/config/state.h"
dump_file "${VITA3K_SRC}/config/src/config.cpp"
dump_file "${VITA3K_SRC}/emuenv/include/emuenv/state.h"
dump_file "${VITA3K_SRC}/emuenv/src/emuenv.cpp" # Corrected path for emuenv.cpp

echo "Dumping SceDisplay module (game's display interface) and related display system files..."
dump_file "${VITA3K_SRC}/modules/SceDisplay/SceDisplay.cpp"
dump_file "${VITA3K_SRC}/modules/SceDisplay/SceDisplay.h"
dump_file "${VITA3K_SRC}/display/include/display/functions.h" # Corrected path for display functions header
dump_file "${VITA3K_SRC}/display/src/display.cpp" # Corrected path for display implementation
dump_file "${VITA3K_SRC}/display/include/display/state.h" # Corrected path for display state header

echo "Dumping renderer core and Vulkan backend files (main rendering loop, caches, synchronization)..."
dump_file "${VITA3K_SRC}/renderer/include/renderer/functions.h" # General renderer functions header
dump_file "${VITA3K_SRC}/renderer/src/renderer.cpp" # General renderer implementation (often orchestrates backend)
dump_file "${VITA3K_SRC}/renderer/include/renderer/state.h" # General renderer state header
dump_file "${VITA3K_SRC}/renderer/include/renderer/types.h" # General renderer types
dump_file "${VITA3K_SRC}/renderer/src/vulkan/renderer.cpp" # Vulkan renderer main loop
dump_file "${VITA3K_SRC}/renderer/src/vulkan/surface_cache.cpp" # Vulkan surface cache
dump_file "${VITA3K_SRC}/renderer/src/vulkan/pipeline_cache.cpp" # Vulkan pipeline cache (shader compilation)
dump_file "${VITA3K_SRC}/renderer/src/vulkan/screen_renderer.cpp" # Vulkan screen renderer (SDL_Window integration)
dump_file "${VITA3K_SRC}/renderer/include/renderer/vulkan/state.h" # Vulkan renderer state
dump_file "${VITA3K_SRC}/renderer/include/renderer/vulkan/types.h" # Vulkan renderer types
dump_file "${VITA3K_SRC}/renderer/src/vulkan/texture.cpp" # Vulkan texture management (often where actual GPU texture operations happen)
dump_file "${VITA3K_SRC}/renderer/src/texture/cache.cpp" # General texture cache logic
dump_file "${VITA3K_SRC}/gxm/include/gxm/state.h" # Graphics context state (for GXM)
dump_file "${VITA3K_SRC}/gxm/include/gxm/functions.h" # Graphics context functions (for GXM)

echo "Dumping kernel timing and thread management (vblank waits, thread scheduling)..."
dump_file "${VITA3K_SRC}/kernel/include/kernel/state.h" # Kernel state (contains threads, mutexes, etc.)
dump_file "${VITA3K_SRC}/kernel/src/kernel.cpp" # Kernel implementation (often includes scheduler logic)
dump_file "${VITA3K_SRC}/kernel/include/kernel/time.h" # Kernel time functions header
dump_file "${VITA3K_SRC}/kernel/src/thread.cpp" # Thread implementation details
dump_file "${VITA3K_SRC}/kernel/include/kernel/thread/thread_state.h" # Thread state structures
dump_file "${VITA3K_SRC}/modules/SceKernelThreadMgr/SceThreadmgr.cpp" # Thread manager module (implements sceKernelDelayThread, etc.)

echo "Dumping audio subsystem (potential sync points, separate threads)..."
dump_file "${VITA3K_SRC}/audio/include/audio/state.h" # Audio state
dump_file "${VITA3K_SRC}/audio/src/audio.cpp" # Audio implementation (callback, mixing, thread wake-up)
dump_file "${VITA3K_SRC}/modules/SceAudio/SceAudio.cpp" # SceAudio module (game's audio interface)
dump_file "${VITA3K_SRC}/audio/include/audio/impl/sdl_audio.h" # SDL audio backend (for audio_callback/swap_window)
dump_file "${VITA3K_SRC}/audio/src/impl/sdl_audio.cpp" # SDL audio backend implementation

echo "Dumping shader recompilation and caching (potential stalls for new pipelines)..."
dump_file "${VITA3K_SRC}/shader/include/shader/spirv_recompiler.h"
dump_file "${VITA3K_SRC}/shader/src/spirv_recompiler.cpp"

echo "Dumping GUI performance overlay and settings (for configuration visibility)..."
dump_file "${VITA3K_SRC}/gui/src/settings_dialog.cpp"
dump_file "${VITA3K_SRC}/gui/src/perf_overlay.cpp" # Corrected path for perf_overlay.cpp

echo "Dumping CMakeLists files for build context (important for module/renderer linking)..."
dump_file "${VITA3K_SRC}/CMakeLists.txt" # Main Vita3K CMakeLists.txt
dump_file "${VITA3K_SRC}/kernel/CMakeLists.txt"
dump_file "${VITA3K_SRC}/gui/CMakeLists.txt"
dump_file "${VITA3K_SRC}/modules/CMakeLists.txt"
dump_file "${VITA3K_SRC}/renderer/CMakeLists.txt"
dump_file "${VITA3K_SRC}/audio/CMakeLists.txt"

echo "✅ File dump complete! Check ${OUTPUT_FILE}"
echo "📊 Total size: $(wc -c < "${OUTPUT_FILE}") bytes"
echo "📄 Total lines: $(wc -l < "${OUTPUT_FILE}") lines"
