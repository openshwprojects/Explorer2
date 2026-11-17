#pragma once
// MemberDetect.h - whether a type has a member of a given name, in C++17 (std::void_t), for readers that accept
// several entry types (archive listings name the path field differently). Same answers as a C++20
// `requires { item.name; }`, which GCC 9 (Ubuntu 20.04, accepted by the Linux build of the readers) cannot compile
// inside generic lambdas.
#include <type_traits>
#include <utility>

namespace Explorer2::MemberDetect {

template <typename T, typename = void> struct HasRelativePath : std::false_type {};
template <typename T> struct HasRelativePath<T, std::void_t<decltype(std::declval<const T&>().relativePath)>> : std::true_type {};

template <typename T, typename = void> struct HasName : std::false_type {};
template <typename T> struct HasName<T, std::void_t<decltype(std::declval<const T&>().name)>> : std::true_type {};

template <typename T, typename = void> struct HasPath : std::false_type {};
template <typename T> struct HasPath<T, std::void_t<decltype(std::declval<const T&>().path)>> : std::true_type {};

template <typename T, typename = void> struct HasFileName : std::false_type {};
template <typename T> struct HasFileName<T, std::void_t<decltype(std::declval<const T&>().fileName)>> : std::true_type {};

} // namespace Explorer2::MemberDetect
