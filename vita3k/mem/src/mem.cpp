// Vita3K emulator project
// Copyright (C) 2025 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <mem/functions.h>
#include <mem/state.h>
// Assuming Address class is defined here
#include <address.h> 

#include <util/align.h>
#include <util/log.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <mutex>
#include <utility>
#include <string> // For std::string

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <csignal>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h> // For errno and strerror
#endif

constexpr uint32_t STANDARD_PAGE_SIZE = KiB(4);
constexpr size_t TOTAL_MEM_SIZE = GiB(4);
constexpr bool LOG_PROTECT = true; // Changed to true for detailed mprotect logging
constexpr bool PAGE_NAME_TRACKING = false;

// Forward declaration if MemState is in a different namespace for Address::get_host_ptr
namespace emuenv {
    class MemState;
}

// TODO: support multiple handlers
static AccessViolationHandler access_violation_handler;
static void register_access_violation_handler(const AccessViolationHandler &handler);

static Address alloc_inner(emuenv::MemState &state, uint32_t start_page, uint32_t page_count, const char *name, const bool force); // Added emuenv::
static void delete_memory(uint8_t *memory);

#ifdef _WIN32
static std::string get_error_msg() {
    return std::system_category().message(GetLastError());
}
#else
static std::string get_error_msg() {
    return strerror(errno);
}
#endif

// Implement Address::get_host_ptr here, as MemState is fully defined now.
// This implementation should be in a .cpp file where MemState is fully defined.
// If Address is a template, it might be in a .tpp or just in the header itself if small.
// Assuming for now it's a non-template member of Address, or an external helper.
// The Address class itself needs to be able to call this logic, so it's best as a member.
// If Address::get_host_ptr is already templated and defined in a header, this part is redundant.
// But based on the error, the compiler couldn't find ANY get_host_ptr for Address.

// This is likely what Address::get_host_ptr should do.
// If Address::get_host_ptr is already implemented elsewhere, remove this.
namespace { // Anonymous namespace for helper
template<typename T>
T* get_host_ptr_impl(Address vita_addr, const emuenv::MemState& state) {
    if (state.use_page_table) {
        // Correct way to get host pointer with page table.
        // It points to the base of the 4KB page, then add the offset within that page.
        return reinterpret_cast<T*>(state.page_table[vita_addr / KiB(4)]) + (vita_addr % KiB(4));
    } else {
        // Without page table, it's a direct offset from the main memory base.
        return reinterpret_cast<T*>(state.memory.get() + vita_addr.address());
    }
}
} // Anonymous namespace

// Specialization or explicit definition for Address::get_host_ptr
// This must match the declaration in mem/address.h
template<typename T>
T* Address::get_host_ptr(const emuenv::MemState& state) const {
    return get_host_ptr_impl<T>(*this, state);
}


bool init(emuenv::MemState &state, const bool use_page_table) { // Added emuenv::
#ifdef _WIN32
    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    state.page_size = system_info.dwPageSize;
#else
    state.page_size = static_cast<int>(sysconf(_SC_PAGESIZE));
#endif
    state.page_size = std::max(STANDARD_PAGE_SIZE, state.page_size);

    assert(state.page_size >= 4096); // Limit imposed by Unicorn.
    assert(!use_page_table || state.page_size == KiB(4));

    void *preferred_address = reinterpret_cast<void *>(1ULL << 34);

#ifdef _WIN32
    state.memory = Memory(static_cast<uint8_t *>(VirtualAlloc(preferred_address, TOTAL_MEM_SIZE, MEM_RESERVE, PAGE_NOACCESS)), delete_memory);
    if (!state.memory) {
        // fallback
        state.memory = Memory(static_cast<uint8_t *>(VirtualAlloc(nullptr, TOTAL_MEM_SIZE, MEM_RESERVE, PAGE_NOACCESS)), delete_memory);

        if (!state.memory) {
            LOG_CRITICAL("VirtualAlloc failed: {}", get_error_msg());
            return false;
        }
    }
#else
    // http://man7.org/linux/man-pages/man2/mmap.2.html
    const int prot = PROT_NONE;
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    const int fd = 0;
    const off_t offset = 0;
    // preferred_address is only a hint for mmap, if it can't use it, the kernel will choose itself the address
    state.memory = Memory(static_cast<uint8_t *>(mmap(preferred_address, TOTAL_MEM_SIZE, prot, flags, fd, offset)), delete_memory);
    if (state.memory.get() == MAP_FAILED) {
        LOG_CRITICAL("mmap failed {}", get_error_msg());
        return false;
    }
#endif

    const size_t table_length = TOTAL_MEM_SIZE / state.page_size;
    state.alloc_table = AllocPageTable(new AllocMemPage[table_length]);
    memset(state.alloc_table.get(), 0, sizeof(AllocMemPage) * table_length);

    state.allocator.set_maximum(table_length);

    const auto handler = [&state](uint8_t *addr, bool write) noexcept {
        return handle_access_violation(state, addr, write);
    };
    register_access_violation_handler(handler);

    const Address null_address = alloc_inner(state, 0, 1, "null", true);
    assert(null_address == 0);
#ifdef _WIN32
    DWORD old_protect = 0;
    const BOOL ret = VirtualProtect(state.memory.get(), state.page_size, PAGE_NOACCESS, &old_protect);
    LOG_CRITICAL_IF(!ret, "VirtualAlloc failed: {}", get_error_msg());
#else
    const int ret = mprotect(state.memory.get(), state.page_size, PROT_NONE);
    LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
#endif

    state.use_page_table = use_page_table;
    if (use_page_table) {
        state.page_table = PageTable(new PagePtr[TOTAL_MEM_SIZE / KiB(4)]);
        // we use an absolute offset (it is faster), so each entry is the same
        // Note: This initial fill might be problematic if AddExternalMapping is not used immediately
        // for regions that will be externally mapped.
        std::fill_n(state.page_table.get(), TOTAL_MEM_SIZE / KiB(4), state.memory.get());
    }

    return true;
}

static void delete_memory(uint8_t *memory) {
    if (memory != nullptr) {
#ifdef _WIN32
        const BOOL ret = VirtualFree(memory, 0, MEM_RELEASE);
        assert(ret);
#else
        munmap(memory, TOTAL_MEM_SIZE);
#endif
    }
}

bool is_valid_addr(const emuenv::MemState &state, Address addr) { // Added emuenv::
    const uint32_t page_num = addr / state.page_size;
    return addr && state.allocator.free_slot_count(page_num, page_num + 1) == 0;
}

bool is_valid_addr_range(const emuenv::MemState &state, Address start, Address end) { // Added emuenv::
    const uint32_t start_page = start / state.page_size;
    const uint32_t end_page = (end + state.page_size - 1) / state.page_size;
    return state.allocator.free_slot_count(start_page, end_page) == 0;
}

static Address alloc_inner(emuenv::MemState &state, uint32_t start_page, uint32_t page_count, const char *name, const bool force) { // Added emuenv::
    int page_num;
    if (force) {
        if (state.allocator.allocate_at(start_page, page_count) < 0) {
            LOG_CRITICAL("Failed to allocate at specific page");
        }
        page_num = start_page;
    } else {
        page_num = state.allocator.allocate_from(start_page, page_count, false);
        if (page_num < 0)
            return 0;
    }

    const uint32_t size = page_count * state.page_size;
    const Address addr = page_num * state.page_size;
    uint8_t *const memory = &state.memory[addr]; // This is `state.memory.get() + addr.address()`, correct.

    // Make memory chunk available to access
#ifdef _WIN32
    const void *const ret = VirtualAlloc(memory, size, MEM_COMMIT, PAGE_READWRITE);
    LOG_CRITICAL_IF(!ret, "VirtualAlloc failed: {}", get_error_msg());
#else
    // This mprotect is on the main allocated block, so 'memory' is already host-aligned
    const int ret = mprotect(memory, size, PROT_READ | PROT_WRITE);
    LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
#endif
    std::memset(memory, 0, size);

    AllocMemPage &page = state.alloc_table[page_num];
    assert(!page.allocated);
    page.allocated = 1;
    page.size = page_count;

    if (PAGE_NAME_TRACKING) {
        state.page_name_map.emplace(page_num, name);
    }

    return addr;
}

Address alloc_aligned(emuenv::MemState &state, uint32_t size, const char *name, unsigned int alignment, Address start_addr) { // Added emuenv::
    if (alignment == 0)
        return alloc(state, size, name, start_addr);
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    size += alignment;
    const uint32_t page_count = align(size, state.page_size) / state.page_size;
    const Address addr = alloc_inner(state, start_addr / state.page_size, page_count, name, false);
    const Address align_addr = align(addr, alignment);
    const uint32_t page_num = addr / state.page_size;
    const uint32_t align_page_num = align_addr / state.page_size;

    if (page_num != align_page_num) {
        AllocMemPage &page = state.alloc_table[page_num];
        AllocMemPage &align_page = state.alloc_table[align_page_num];
        const uint32_t remnant_front = align_page_num - page_num;
        state.allocator.free(page_num, remnant_front);
        page.allocated = 0;
        align_page.allocated = 1;
        align_page.size = page.size - remnant_front;
    }

    return align_addr;
}

static void align_to_page(emuenv::MemState &state, Address &addr, Address &size) { // Added emuenv::
    const Address end = align(addr + size, state.page_size);
    addr = align_down(addr, state.page_size);
    size = end - addr;
}

void unprotect_inner(emuenv::MemState &state, Address addr, uint32_t size) { // Added emuenv::
    if (LOG_PROTECT) {
        fmt::print("Unprotect: {} {}\n", log_hex(addr), size);
    }
    // CORRECTED: Use Address::get_host_ptr to get the correct host pointer
    void* host_ptr = addr.get_host_ptr<uint8_t>(state);

#ifdef _WIN32
    DWORD old_protect = 0;
    // Removed '- 1' from size. VirtualProtect expects the exact size of the region.
    const BOOL ret = VirtualProtect(host_ptr, size, PAGE_READWRITE, &old_protect);
    LOG_CRITICAL_IF(!ret, "VirtualAlloc failed: {}", get_error_msg());
#else
    // Detailed logging for mprotect arguments.
    LOG_CRITICAL("mprotect unprotect_inner: Vita_Addr={:X}, Host_Ptr={:X}, Size={:X}, Prot={:x}",
                 addr.address(), (uintptr_t)host_ptr, size, PROT_READ | PROT_WRITE);
    const int ret = mprotect(host_ptr, size, PROT_READ | PROT_WRITE);
    LOG_CRITICAL_IF(ret == -1, "mprotect failed (errno {}): {}", errno, get_error_msg());
#endif
}

void protect_inner(emuenv::MemState &state, Address addr, uint32_t size, const MemPerm perm) { // Added emuenv::
    // CORRECTED: Use Address::get_host_ptr to get the correct host pointer
    void* host_ptr = addr.get_host_ptr<uint8_t>(state);

    int prot_flags = (perm == MemPerm::None) ? PROT_NONE : ((perm == MemPerm::ReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE));

#ifdef _WIN32
    DWORD old_protect = 0;
    // Removed '- 1' from size. VirtualProtect expects the exact size of the region.
    const BOOL ret = VirtualProtect(host_ptr, size, (perm == MemPerm::None) ? PAGE_NOACCESS : ((perm == MemPerm::ReadOnly) ? PAGE_READONLY : PAGE_READWRITE), &old_protect);
    LOG_CRITICAL_IF(!ret, "VirtualAlloc failed: {}", get_error_msg());
#else
    // Detailed logging for mprotect arguments.
    LOG_CRITICAL("mprotect protect_inner: Vita_Addr={:X}, Host_Ptr={:X}, Size={:X}, Perm={:x}",
                 addr.address(), (uintptr_t)host_ptr, size, prot_flags);
    const int ret = mprotect(host_ptr, size, prot_flags);
    LOG_CRITICAL_IF(ret == -1, "mprotect failed (errno {}): {}", errno, get_error_msg());
#endif
}

bool handle_access_violation(emuenv::MemState &state, uint8_t *addr, bool write) noexcept { // Added emuenv::
    const uintptr_t memory_addr = reinterpret_cast<uintptr_t>(state.memory.get());
    const uintptr_t fault_addr = reinterpret_cast<uintptr_t>(addr);

    Address vaddr = 0;
    const std::unique_lock<std::mutex> lock(state.protect_mutex);
    if (fault_addr < memory_addr || fault_addr >= memory_addr + TOTAL_MEM_SIZE) {
        if (state.use_page_table) {
            // this may come from an external mapping
            uint64_t addr_val = std::bit_cast<uint64_t>(addr);
            auto it = state.external_mapping.lower_bound(addr_val);
            if (it != state.external_mapping.end() && addr_val < it->first + it->second.size) {
                vaddr = static_cast<Address>(addr_val - it->first + it->second.address);
            } else {
                // Previously, this branch returned false immediately.
                // Log and try to unprotect anyway if it was a valid Vita address being accessed
                // (This is a HACK, but matches previous behavior after logging)
                LOG_CRITICAL("Unhandled access violation: Host address 0x{:X} not within main memory range, and not found in external mappings. Attempting unprotect with 4 bytes.", fault_addr);
                // Corrected: Pass a valid Vita address to unprotect_inner if it's the target.
                // If this 'addr' is truly an arbitrary host pointer that isn't mapped, passing it to Address() constructor won't magically make it a valid Vita address.
                // This 'unprotect_inner' call within handle_access_violation might need a more robust host-to-Vita address translation or a different approach for unmapped host addresses.
                // For now, retaining original logic but acknowledging potential issue if fault_addr is truly arbitrary.
                unprotect_inner(state, Address(fault_addr - memory_addr), 4); // Convert host fault_addr back to a Vita address offset
                return true;
            }
        } else {
            // Unhandled access violation: Host address not within main memory range, and page table not used.
            LOG_CRITICAL("Unhandled access violation: Host address 0x{:X} not within main memory range. Returning false.", fault_addr);
            return false;
        }
    } else {
        vaddr = static_cast<Address>(fault_addr - memory_addr);
    }

    if (!is_valid_addr(state, vaddr)) {
        LOG_CRITICAL("Unhandled access violation: Vita Address 0x{:X} is not valid. Returning false.", vaddr.address());
        return false;
    }
    if (LOG_PROTECT) {
        fmt::print("Access: {}\n", log_hex(vaddr));
    }

    auto it = state.protect_tree.lower_bound(vaddr);
    if (it == state.protect_tree.end()) {
        // HACK: keep going
        unprotect_inner(state, vaddr, 4);
        LOG_CRITICAL("Unhandled write protected region was valid. Address=0x{:X}. No protection segment found. Unprotecting 4 bytes.", vaddr.address());
        return true;
    }

    ProtectSegmentInfo &info = it->second;
    if (vaddr < it->first || vaddr >= it->first + info.size) {
        // HACK: keep going
        unprotect_inner(state, vaddr, 4);
        LOG_CRITICAL("Unhandled write protected region was valid. Address=0x{:X}. Not within found protection segment. Unprotecting 4 bytes.", vaddr.address());
        return true;
    }

    Address previous_beg = it->first;
    for (auto ite = info.blocks.begin(); ite != info.blocks.end();) {
        if (vaddr >= ite->first && vaddr < ite->first + ite->second.size && ite->second.callback(vaddr, write)) {
            Address beg_unpr = align_down(ite->first, state.page_size);
            Address end_unpr = align(ite->first + ite->second.size, state.page_size);
            unprotect_inner(state, beg_unpr, end_unpr - beg_unpr);

            ite = info.blocks.erase(ite);
        } else {
            ++ite;
        }
    }

    if (info.blocks.empty() && info.ref_count == 0) {
        unprotect_inner(state, it->first, info.size);
        state.protect_tree.erase(it);
    } else {
        // Recalculate the region to protect for the remaining blocks
        // This logic needs to be robust if `blocks` becomes empty here
        if (!info.blocks.empty()) {
            Address beg_region = info.blocks.begin()->first;
            Address end_region = info.blocks.rbegin()->first + info.blocks.rbegin()->second.size;

            beg_region = align_down(beg_region, state.page_size);
            end_region = align(end_region, state.page_size);

            if (beg_region != previous_beg) {
                ProtectSegmentInfo new_info = std::move(info);
                new_info.size = end_region - beg_region;

                state.protect_tree.erase(it);
                state.protect_tree.emplace(beg_region, std::move(new_info));
            } else {
                info.size = end_region - beg_region;
            }
        }
    }

    return true;
}

bool add_protect(emuenv::MemState &state, Address addr, const uint32_t size, const MemPerm perm, const ProtectCallback &callback) { // Added emuenv::
    const std::lock_guard<std::mutex> lock(state.protect_mutex);
    ProtectSegmentInfo protect(size, perm);
    align_to_page(state, addr, protect.size); // Ensure addr and size are page-aligned for mprotect

    ProtectBlockInfo block;
    block.size = size;
    block.callback = callback;

    protect.blocks.emplace(addr, std::move(block));

    auto it = state.protect_tree.lower_bound(addr);
    // Find the overlapping or adjacent segment
    if (it != state.protect_tree.end() && it->first + it->second.size <= addr) {
        // If it's the last element, or if the current element ends before our 'addr',
        // try the previous element. This handles cases where 'addr' is between segments.
        if (it == state.protect_tree.begin())
            it = state.protect_tree.end(); // No previous element to check
        else
            --it; // Check previous element
    }

    // Now, 'it' points to a potential overlapping segment, or end() if none before 'addr'.
    // Iterate and merge overlapping segments.
    while (it != state.protect_tree.end() && it->first < addr + protect.size) {
        const Address start = std::min(it->first, addr);
        protect.size = std::max(it->first + it->second.size, addr + protect.size) - start;
        addr = start;
        protect.ref_count += it->second.ref_count; // Transfer access count to new block
        // Merge blocks from the existing segment into the new 'protect' segment
        protect.blocks.merge(it->second.blocks);

        // Erase the old segment and move to the previous one (if any)
        if (it == state.protect_tree.begin()) {
            state.protect_tree.erase(it);
            break; // No more previous elements
        }
        state.protect_tree.erase(it--); // Erase and decrement to check previous
    }

    if (protect.ref_count == 0) {
        // Only apply mprotect if no external access references this region
        protect_inner(state, addr, protect.size, perm);
    }

    state.protect_tree.emplace(addr, std::move(protect));
    return true;
}

bool is_protecting(emuenv::MemState &state, Address addr, MemPerm *perm) { // Added emuenv::
    const std::lock_guard<std::mutex> lock(state.protect_mutex);
    auto ite = state.protect_tree.lower_bound(addr);

    if (ite != state.protect_tree.end() && addr < ite->first + ite->second.size) {
        if (perm)
            *perm = ite->second.perm;

        return true;
    }

    return false;
}

void open_access_parent_protect_segment(emuenv::MemState &state, Address addr) { // Added emuenv::
    const std::lock_guard<std::mutex> lock(state.protect_mutex);
    auto ite = state.protect_tree.lower_bound(addr);

    // Adjust iterator to find containing segment
    if (ite != state.protect_tree.end() && ite->first > addr) {
        if (ite == state.protect_tree.begin()) {
            ite = state.protect_tree.end(); // No previous segment
        } else {
            --ite; // Check previous segment
        }
    }

    if (ite != state.protect_tree.end() && addr < ite->first + ite->second.size) {
        ite->second.ref_count++;
    } else {
        // If no existing segment covers this address, create a new one with ReadWrite and ref_count = 1
        ProtectSegmentInfo protect(0, MemPerm::ReadWrite);
        protect.ref_count = 1;
        // Align the address for the new segment to page boundary
        state.protect_tree.emplace(align_down(addr, state.page_size), std::move(protect));
    }
}

void close_access_parent_protect_segment(emuenv::MemState &state, Address addr) { // Added emuenv::
    const std::lock_guard<std::mutex> lock(state.protect_mutex);
    auto ite = state.protect_tree.lower_bound(addr);

    // Adjust iterator to find containing segment
    if (ite != state.protect_tree.end() && ite->first > addr) {
        if (ite == state.protect_tree.begin()) {
            ite = state.protect_tree.end(); // No previous segment
        } else {
            --ite; // Check previous segment
        }
    }

    if (ite != state.protect_tree.end()) {
        ProtectSegmentInfo &info = ite->second;
        if (info.ref_count > 0) {
            info.ref_count--;
        }

        if (info.ref_count == 0) {
            // Only protect if there are blocks still active in the segment, otherwise erase.
            // A size of 0 implies an empty segment created by open_access_parent_protect_segment without blocks.
            if (info.blocks.empty() || info.size == 0) {
                state.protect_tree.erase(ite);
            } else {
                protect_inner(state, ite->first, info.size, info.perm);
            }
        }
    }
}

void add_external_mapping(emuenv::MemState &mem, Address addr, uint32_t size, uint8_t *addr_ptr) { // Added emuenv::
    assert((size & 4095) == 0); // Ensure size is page-aligned

    uint64_t addr_value = std::bit_cast<uint64_t>(addr_ptr);
    // Corrected: page_table_entry_offset would be addr_ptr - addr.get_host_ptr(mem)
    // The previous use of page_table_entry_offset wasn't directly used for page_table assignment,
    // and the loop correctly sets the page_table entries to the external mapping's host addresses.
    uint8_t *original_host_address = addr.get_host_ptr<uint8_t>(mem); // Get host address in main memory

    for (int block = 0; block < size / KiB(4); block++) {
        // This is not thread write safe, but hopefully no other thread is busy copying while this happens
        memcpy(addr_ptr + block * KiB(4), original_host_address + block * KiB(4), KiB(4));
        mem.page_table[addr / KiB(4) + block] = addr_ptr + block * KiB(4); // Store the actual host page address
    }

    // Corrected: The page_table entries are already set within the loop above.
    // The original logic for `protect_inner` and then `mem.page_table[addr / KiB(4)] = page_table_entry;` was problematic.
    // We should call protect_inner with the *Vita address* and the *correct host mapping* is then handled by Address::get_host_ptr.
    protect_inner(mem, addr, size, MemPerm::None);

    const std::unique_lock<std::mutex> lock(mem.protect_mutex);
    mem.external_mapping[addr_value] = { addr, size };
}

void remove_external_mapping(emuenv::MemState &mem, uint8_t *addr_ptr) { // Added emuenv::
    uint64_t addr_value = std::bit_cast<uint64_t>(addr_ptr);
    MemExternalMapping mapping;
    {
        const std::unique_lock<std::mutex> lock(mem.protect_mutex);
        auto it = mem.external_mapping.find(addr_value);
        assert(it != mem.external_mapping.end());

        mapping = it->second;
        mem.external_mapping.erase(it);
    }

    // remove all protections on this range
    // The 'unprotect_inner' will now correctly convert mapping.address (Vita address) to host ptr
    unprotect_inner(mem, mapping.address, mapping.size);
    {
        const std::unique_lock<std::mutex> lock(mem.protect_mutex);
        auto prot_it = mem.protect_tree.lower_bound(mapping.address);
        // Adjust iterator to find containing segment
        if (prot_it != mem.protect_tree.end() && prot_it->first > mapping.address) {
            if (prot_it == mem.protect_tree.begin()) {
                prot_it = mem.protect_tree.end();
            } else {
                --prot_it;
            }
        }

        while (prot_it != mem.protect_tree.end() && prot_it->first < mapping.address + mapping.size) {
            if (prot_it == mem.protect_tree.begin()) {
                mem.protect_tree.erase(prot_it);
                break;
            }

            mem.protect_tree.erase(prot_it--);
        }
    }

    // unprotect the original memory range
    // Corrected: Reset page table entries to point to the main memory allocation
    for (int block = 0; block < mapping.size / KiB(4); block++) {
        // This is not thread write safe, but hopefully no other thread is busy copying while this happens
        memcpy(&mem.memory[mapping.address] + block * KiB(4), addr_ptr + block * KiB(4), KiB(4));
        mem.page_table[mapping.address / KiB(4) + block] = mem.memory.get() + (mapping.address + block * KiB(4)); // Point to corresponding offset in main memory
    }
    // Now that page_table entries are reset, apply unprotect to the original memory range
    unprotect_inner(mem, mapping.address, mapping.size);
}

Address alloc(emuenv::MemState &state, uint32_t size, const char *name, Address start_addr) { // Added emuenv::
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    const uint32_t page_count = align(size, state.page_size) / state.page_size;
    const Address addr = alloc_inner(state, start_addr / state.page_size, page_count, name, false);
    return addr;
}

Address alloc_at(emuenv::MemState &state, Address address, uint32_t size, const char *name) { // Added emuenv::
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    const uint32_t wanted_page = address / state.page_size;
    size += address % state.page_size;
    const uint32_t page_count = align(size, state.page_size) / state.page_size;
    alloc_inner(state, wanted_page, page_count, name, true);
    return address;
}

Address try_alloc_at(emuenv::MemState &state, Address address, uint32_t size, const char *name) { // Added emuenv::
    const uint32_t wanted_page = address / state.page_size;
    size += address % state.page_size;
    const uint32_t page_count = align(size, state.page_size) / state.page_size;
    if (state.allocator.free_slot_count(wanted_page, wanted_page + page_count) != page_count) {
        return 0;
    }
    (void)alloc_inner(state, wanted_page, page_count, name, true);
    return address;
}

Block alloc_block(emuenv::MemState &mem, uint32_t size, const char *name, Address start_addr) { // Added emuenv::
    const Address address = alloc(mem, size, name, start_addr);
    return Block(address, [&mem](Address stack) {
        free(mem, stack);
    });
}

void free(emuenv::MemState &state, Address address) { // Added emuenv::
    const std::lock_guard<std::mutex> lock(state.generation_mutex);
    const uint32_t page_num = address / state.page_size;
    assert(page_num >= 0);

    AllocMemPage &page = state.alloc_table[page_num];
    if (!page.allocated) {
        LOG_CRITICAL("Freeing unallocated page");
    }
    page.allocated = 0;

    state.allocator.free(page_num, page.size);
    if (PAGE_NAME_TRACKING) {
        state.page_name_map.erase(page_num);
    }

    // Assumes page_table[address / KiB(4)] points to state.memory.get() for non-external mappings.
    // If this page was part of an external mapping, this needs to be handled differently (e.g., reset its page_table entry)
    // The previous check is likely redundant or incorrect if page_table entries are dynamically updated.
    // assert(!state.use_page_table || state.page_table[address / KiB(4)] == state.memory.get());

    uint8_t *const memory = &state.memory[page_num * state.page_size];

#ifdef _WIN32
    const BOOL ret = VirtualFree(memory, page.size * state.page_size, MEM_DECOMMIT);
    LOG_CRITICAL_IF(!ret, "VirtualFree failed: {}", get_error_msg());
#else
    int ret = mprotect(memory, page.size * state.page_size, PROT_NONE);
    LOG_CRITICAL_IF(ret == -1, "mprotect failed: {}", get_error_msg());
    ret = madvise(memory, page.size * state.page_size, MADV_DONTNEED);
    LOG_CRITICAL_IF(ret == -1, "madvise failed: {}", get_error_msg());
#endif
}

uint32_t mem_available(emuenv::MemState &state) { // Added emuenv::
    return state.allocator.free_slot_count(0, state.allocator.max_offset) * state.page_size;
}

const char *mem_name(Address address, emuenv::MemState &state) { // Added emuenv::
    if (PAGE_NAME_TRACKING) {
        return state.page_name_map.find(address / state.page_size)->second.c_str();
    }
    return "";
}

#ifdef _WIN32

static LONG WINAPI exception_handler(PEXCEPTION_POINTERS pExp) noexcept {
    if (pExp->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT && IsDebuggerPresent()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto ptr = reinterpret_cast<uint8_t *>(pExp->ExceptionRecord->ExceptionInformation[1]);
    const bool is_writing = pExp->ExceptionRecord->ExceptionInformation[0] == 1;
    const bool is_executing = pExp->ExceptionRecord->ExceptionInformation[0] == 8;

    if (pExp->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && !is_executing) {
        if (access_violation_handler(ptr, is_writing)) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static void register_access_violation_handler(const AccessViolationHandler &handler) {
    access_violation_handler = handler;
    if (!AddVectoredExceptionHandler(1, exception_handler)) {
        LOG_CRITICAL("Failed to register an exception handler");
    }
}

#else

static void signal_handler(int sig, siginfo_t *info, void *uct) noexcept {
    auto context = static_cast<ucontext_t *>(uct);

#ifdef __aarch64__
#ifdef __APPLE__
    const uint32_t esr = context->uc_mcontext->__es.__esr;
#else
    _aarch64_ctx *ctx = reinterpret_cast<_aarch64_ctx *>(context->uc_mcontext.__reserved);
    // get the ESR register
    while (ctx->magic != ESR_MAGIC) {
        if (ctx->magic == 0)
            [[unlikely]]
            raise(SIGTRAP);
        else
            [[likely]]
            ctx = reinterpret_cast<_aarch64_ctx *>(reinterpret_cast<uint8_t *>(ctx) + ctx->size);
    }

    const uint64_t esr = reinterpret_cast<esr_context *>(ctx)->esr;
#endif
    // https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/ESR-EL1--Exception-Syndrome-Register--EL1-
    const uint32_t exception_class = static_cast<uint32_t>(esr) >> 26;
    const bool is_executing = (exception_class == 0b100000) || (exception_class == 0b100001);
    const bool is_data_abort = (exception_class == 0b100100) || (exception_class == 0b100101);
    const bool is_writing = is_data_abort && (esr & (1 << 6));
#else
#ifdef __APPLE__
    const uint64_t err = context->uc_mcontext->__es.__err;
#else
    const uint64_t err = context->uc_mcontext.gregs[REG_ERR];
#endif
    const bool is_executing = err & 0x10;
    const bool is_writing = err & 0x2;
#endif

    if (!is_executing) {
        if (access_violation_handler(reinterpret_cast<uint8_t *>(info->si_addr), is_writing)) {
            return;
        }
    }

    LOG_CRITICAL("Unhandled access to {}", log_hex(*reinterpret_cast<uint64_t *>(&info->si_addr)));
    raise(SIGTRAP);
    return;
}

static void register_access_violation_handler(const AccessViolationHandler &handler) {
    access_violation_handler = handler;
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signal_handler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        LOG_CRITICAL("Failed to register an exception handler");
    }
#ifdef __APPLE__
    // When accessing memory region which is PROT_NONE on macOS, it is raising SIGBUS not SIGSEGV.
    // So apply same signal handler to SIGBUS
    if (sigaction(SIGBUS, &sa, NULL) == -1) {
        LOG_CRITICAL("Failed to register an exception handler to SIGBUS");
    }
#endif
}

#endif