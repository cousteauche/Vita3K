// vita3k/mem/include/mem/address.h
#pragma once

#include <cstdint> // For uint32_t, uint64_t

// Forward declare MemState for Address::get_host_ptr
namespace emuenv {
    class MemState;
}

class Address {
public:
    // Constructors
    constexpr Address() : m_value(0) {}
    constexpr Address(uint32_t value) : m_value(value) {}
    constexpr Address(uint64_t value) : m_value(static_cast<uint32_t>(value)) {} // If Vita uses 32-bit addresses

    // Implicit conversion to uint32_t. This is what makes it "aka 'unsigned int'"
    // This is fine as long as the compiler also sees the member functions.
    constexpr operator uint32_t() const { return m_value; }

    // Member function to get the raw address value
    constexpr uint32_t address() const { return m_value; }

    // Template member function to convert to host pointer.
    // The implementation of this function (template specialization)
    // is provided directly in mem.cpp for full MemState access.
    template<typename T>
    T* get_host_ptr(const emuenv::MemState& state) const;

private:
    uint32_t m_value; // Assuming 32-bit Vita addresses
};