// SPDX-License-Identifier: GPL-3.0-only
// Usina - objects made in zeroed memory, as a module makes its globals. The module ports' processors (fx/texture,
// fx/clouds) are globals on their modules, so they start zeroed (static storage), and upstream relies on it: their
// constructors are empty and Init leaves some members as they were (found 21/10 with the texture: on the heap its
// gain control began as garbage - a click, a gain of thousands). So they are made here: calloc, placement new.
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
Zeroed<T> makeZeroed()                                   // (allocates: never on the audio thread)
{
    void* m = std::calloc(1, sizeof(T));
    if (m == nullptr) throw std::bad_alloc();
    return Zeroed<T>(new (m) T);                         // (default-initialised: the empty constructor keeps the zeros)
}

template <typename T>
void rezero(Zeroed<T>& p)                                // (the same memory, zeroed and made again: no allocation)
{
    T* m = p.get();
    m->~T();
    std::memset(static_cast<void*>(m), 0, sizeof(T));
    new (m) T;
}

} // namespace usina
