// This file implements the span container; a light abstraction of the standard pointer + size idiom from C.
//
// A span is a non-owning reference to an existing array of contiguous elements in memory.
// Being non-owning, the referenced data must outlive the span itself.
//
// Spans referencing const data can be constructed implicitly, while spans over mutable data must be explicitly constructed.
// Spans can be constructed from:
//  * A pointer and size
//  * Begin and end pointers
//  * C-style arrays
//  * A std::initializer_list
//      - Should be used with care, see note on initializer lists below.
//  * Other compatible container types
//      - For a container type to be compatible, it must simply supply the default data() and size() methods.
//      - data() must return a pointer to the underlying elements and size() must have an integral return type.

// NOTE:
// 
// Initializer lists only refer to temporary arrays. An initializer list used to construct a span thus must outlive the span itself.
// As such, the following code is wrong:
//
//      span<const int> s({2, 2});
//      int v = sum(s);             // Array referenced by s is invalid!
//
// Even when the elements of the initializer list are not temporary, the underlying array is.
// The following code is thus equally incorrect:
//
//      int i = 0;
//      span<const int> s({i});
//      int v = sum(s);             // Array referenced by s is invalid!
//
// As c++ guarantees that temporaries declared in the function parameter list outlast the function body,
// this is one of the few cases where constructing from an initializer list is safe.
//
//      int v = sum({1, 2, 3});     // The array referenced by the span is guaranteed to outlive the function body of sum.
//

#pragma once
#include <initializer_list>
#include <concepts>

// Specifies that type P is a pointer to a type convertible to T, while disallowing conversions from types deriving from T.
//
// These conversions are prevented by adding an extra pointer indirection to the convertible_to check.
// This works since, while a Derived* can be converted to a Base*, a Derived** cannot be converted to a Base**.
template<typename P, typename T>
concept pointer_to = std::convertible_to<std::decay_t<P>*, T* const*>;

// Specifies that a container type C must provide 
//  * a noexcept size() method with integral return type
//  * a noexcept data() method returning a pointer to a type that is convertible to T, while disallowing pointers to types deriving from T.
template<typename C, typename T>
concept Container = requires (C c) {
    { c.size() } noexcept -> std::integral;
    { c.data() } noexcept -> pointer_to<T>;
};

// A non-owning reference to a contiguous array of elements of type T
template <typename T>
class span {
public:
    using span_type = span<T>;

    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    // Constructs span from a pointer and size.
    constexpr span(pointer ptr, size_type size) noexcept
        : m_data(ptr), m_size(size) {}

    // Constructs span over the range [begin, end)
    constexpr span(pointer begin, pointer end) noexcept
        : span(begin, difference_type(end - begin)) {}

    // Implicitly construct a read-only span from a compatible container
    constexpr span(Container<element_type> auto& c) noexcept
        requires (std::is_const_v<std::remove_reference_t<element_type>>)
    : span(c.data(), c.size()) {}

    // Explicitly construct a mutable span from a compatible container
    explicit constexpr span(Container<element_type> auto& c) noexcept
        : span(c.data(), c.size()) {}

    // Constructs span from a c-style array
    template <size_type Extent>
    constexpr span(element_type(&arr)[Extent]) noexcept : span(arr, Extent) {}

    // Constructs a read-only span from initializer_list.
    // Note that the constructed span is only valid for the lifetime of the initializer_list
    constexpr span(const std::initializer_list<value_type>&& l) noexcept
        requires(std::is_const_v<element_type>)
    : span(l.begin(), l.size()) {}

    // Iterators
    constexpr const_pointer begin() const noexcept { return m_data; }
    constexpr const_pointer end()   const noexcept { return m_data + m_size; }

    // Element access
    constexpr reference       operator[] (size_t i) noexcept { return m_data[i]; }
    constexpr const_reference operator[] (size_t i) const noexcept { return m_data[i]; }

    constexpr reference       front()       noexcept { return m_data[0]; }
    constexpr const_reference front() const noexcept { return m_data[0]; }
    constexpr reference       back()        noexcept { return m_data[m_size - 1]; }
    constexpr const_reference back()  const noexcept { return m_data[m_size - 1]; }
    constexpr pointer         data()  const noexcept { return m_data; }

    // Observers
    constexpr size_type size()       const noexcept { return m_size; }
    constexpr size_type size_bytes() const noexcept { return size() * sizeof(element_type); }
    constexpr bool empty()           const noexcept { return size() == 0; }

    // Subviews
    constexpr span<T> first(size_t count) const noexcept { return span<T>(begin(), count); }
    constexpr span<T> last(size_t count)  const noexcept { return span<T>(end() - count, count); }

    constexpr span<T> subspan(size_t offset, size_t count) const noexcept { return span<T>(begin() + offset, count); }

private:
    pointer     m_data;
    size_type   m_size;
};

#ifdef TEST_SPAN
#include <vector>
// TODO: test implicit/explicit constructors
namespace {
    // Type S can be noexcept constructed using an instance of type C.
    template <typename C, typename S>
    constexpr bool can_construct = requires(C v) { { S(v) } noexcept -> std::same_as<S>; };

    // Type S can be noexcept constructed using a temporary of type C.
    template <typename C, typename S>
    constexpr bool can_construct_temporary = requires(C && v) { { S(std::forward<C&&>(v)) } noexcept -> std::same_as<S>; };

    // Initializer lists can only be used to construct views of const data.
    // Additionally, the initializer list must be a temporary.
    static_assert(not can_construct<std::initializer_list<int>, span<int>>);
    static_assert(not can_construct<std::initializer_list<int>, span<const int>>);
    static_assert(not can_construct_temporary<std::initializer_list<int>, span<int>>);
    static_assert(can_construct_temporary<std::initializer_list<int>, span<const int>>);

    // Make sure we cannot construct a span from a vector of a derived type.
    class A {};
    class B : public A {};

    static_assert(can_construct<std::vector<A>, span<A>>);
    static_assert(not can_construct<std::vector<B>, span<A>>);

    // The element type of a container must be less or equally const-qualified as the span.
    static_assert(can_construct<std::vector<int>, span<int>>);
    static_assert(can_construct<std::vector<int>, span<const int>>);
    static_assert(not can_construct<const std::vector<int>, span<int>>);
    static_assert(can_construct<const std::vector<int>, span<const int>>);

    static_assert(can_construct<std::vector<int*>, span<int*>>);
    static_assert(not can_construct<std::vector<int*>, span<const int*>>);      // Intellisense wrongly says this is an error. Compiles without issue.
    static_assert(can_construct<std::vector<int*>, span<int* const>>);
    static_assert(can_construct<std::vector<int*>, span<const int* const>>);

    static_assert(not can_construct<const std::vector<int*>, span<int*>>);
    static_assert(not can_construct<const std::vector<int*>, span<const int*>>);
    static_assert(can_construct<const std::vector<int*>, span<int* const>>);
    static_assert(can_construct<const std::vector<int*>, span<const int* const>>);

    static_assert(not can_construct<const std::vector<const int*>, span<int*>>);
    static_assert(not can_construct<const std::vector<const int*>, span<const int*>>);
    static_assert(not can_construct<const std::vector<const int*>, span<int* const>>);
    static_assert(can_construct<const std::vector<const int*>, span<const int* const>>);
}
#endif
