// SPDX-License-Identifier: GPL-3.0-only
// Zero-initialized storage for DSP classes that assume static-storage initialization.
// Empty constructors and partial Init methods may otherwise leave state uninitialized.
// Allocate with calloc and construct in place; see reset() for allocation-free reuse.
#pragma once
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace usina {

template <typename T>
struct ZeroedDelete {
    void operator()(T* p) const
    {
        p->~T();
        std::free(p);
    }
};
template <typename T>
using Zeroed = std::unique_ptr<T, ZeroedDelete<T>>;

template <typename T>
Zeroed<T> makeZeroed()                                   // Allocates: never on the audio thread
{
    void* m = std::calloc(1, sizeof(T));
    if (m == nullptr) throw std::bad_alloc();
    return Zeroed<T>(new (m) T);                         // Default-initialised: the empty constructor keeps the zeros
}

template <typename T>
void rezero(Zeroed<T>& p)                                // The same memory, zeroed and made again: no allocation
{
    T* m = p.get();
    m->~T();
    std::memset(static_cast<void*>(m), 0, sizeof(T));
    new (m) T;
}

} // Namespace usina
