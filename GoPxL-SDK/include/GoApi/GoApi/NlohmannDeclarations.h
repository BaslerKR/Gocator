/**
 * @file    NlohmannDeclarations.h
 * @brief   Forward declarations for nlohmann::json types.
 *
 * @internal
 * Copyright (C) 2026 by LMI Technologies Inc.
 * Licensed under the MIT License.
 * Redistributed files must retain the above copyright notice.
 */
#pragma once

#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

/**
 * Forward declare nlohmann types. This allows interface and header files involving json to get around including nlohmann/json.hpp.
 */
namespace nlohmann
{
    namespace detail
    {
        template<typename BasicJsonType>
        class iter_impl;

        class exception;
    }

    template<typename T, typename SFINAE>
    struct adl_serializer;

    template
    <
        template<typename U, typename V, typename... Args> class ObjectType,
        template<typename U, typename... Args> class ArrayType,
        class StringType, class BooleanType,
        class NumberIntegerType,
        class NumberUnsignedType,
        class NumberFloatType,
        template<typename U> class AllocatorType,
        template<typename T, typename SFINAE> class JSONSerializer,
        class BinaryType
    >
    class basic_json;

    template<typename BasicJsonType>
    class json_pointer;

    using json = basic_json<std::map, std::vector, std::string, bool, std::int64_t, std::uint64_t, double, std::allocator, adl_serializer, std::vector<std::uint8_t>>;
}
