#pragma once

#include "Graphics.h"

#include <Windows.h> // ugh

template <typename T, typename H>
class Pool {
public:
    Pool() {
        m_capacity = 128;
        m_free_top = m_capacity - 1;

        // Reserve virtual address space for max potential size of the pool arrays
        m_data       =   (T*)VirtualAlloc(0, sizeof(T)   * UINT16_MAX, MEM_RESERVE, PAGE_READWRITE);
        m_generation = (u16*)VirtualAlloc(0, sizeof(u16) * UINT16_MAX, MEM_RESERVE, PAGE_READWRITE);
        m_freelist   = (u16*)VirtualAlloc(0, sizeof(u16) * UINT16_MAX, MEM_RESERVE, PAGE_READWRITE);

        // Commit memory of the virtual address space for the first m_capacity slots
        VirtualAlloc(m_data,       sizeof(T)   * m_capacity, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(m_generation, sizeof(u16) * m_capacity, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(m_freelist,   sizeof(u16) * m_capacity, MEM_COMMIT, PAGE_READWRITE);

        // Initialize the queue of free indices
        for (int i = 1; i < m_capacity; i++)
            m_freelist[i] = m_capacity - i;
    }

    ~Pool() {
        VirtualFree(m_data,       0, MEM_RELEASE);
        VirtualFree(m_generation, 0, MEM_RELEASE);
        VirtualFree(m_freelist,   0, MEM_RELEASE);
    }

    [[nodiscard]] Handle<H> allocate() {
        // grow if we have run out of free slot indices.
        if (m_free_top == 0) { grow(); }

        Handle<H> handle;

        // pop a free index from the freelist and get the corresponding generation
        handle.index = m_freelist[m_free_top--];
        handle.generation = m_generation[handle.index];

        // return a handle to the added element
        return handle;
    }

    template <typename... Args>
    [[nodiscard]] Handle<H> emplace(Args&&... args) {
        Handle<H> handle = allocate();

        // placement new construct the element at the index in the pool
        new (m_data + handle.index) T(std::forward<Args>(args)...);

        return handle;
    }

    [[nodiscard]] Handle<H> insert(const T& data) { 
        return emplace(data); 
    };

    bool valid(Handle<H> handle) const {
        if (handle.index == 0)                               return false;
        if (handle.index >= m_capacity)                      return false;
        if (handle.generation != m_generation[handle.index]) return false;

        return true;
    }

    void free(Handle<H> handle) {
        // Make sure the handle is valid
        if (!valid(handle)) return;

        // Increment generation counter to indicate the object is now dead.
        m_generation[handle.index]++;

        // Push the newly freed pool index to the freelist
        m_freelist[++m_free_top] = handle.index;
    }

    T* get(Handle<H> handle) const {
        // Make sure the handle is valid
        Check(handle.index > 0,                                "Invalid Handle");
        Check(handle.index < m_capacity,                       "Invalid Handle");
        Check(handle.generation == m_generation[handle.index], "Invalid Handle");

        return m_data + handle.index;
    };

    u32 size() {
        return m_capacity - m_free_top - 1;
    }

private:
    void grow() {
        // Update the freelist top
        m_free_top = m_capacity;

        // Double the pool capacity
        m_capacity *= 2;

        // Commit pages to fit the new pool capacity
        VirtualAlloc(m_data,       sizeof(T)   * m_capacity, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(m_generation, sizeof(u16) * m_capacity, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(m_freelist,   sizeof(u16) * m_capacity, MEM_COMMIT, PAGE_READWRITE);

        // Add indices of the new pool slots being allocated to the freelist
        for (int i = m_free_top; i > 0; i--)
            m_freelist[i] = m_capacity - i;
    }

    u16  m_capacity;
    u16  m_free_top;

    T*   m_data;
    u16* m_generation;
    u16* m_freelist;
};