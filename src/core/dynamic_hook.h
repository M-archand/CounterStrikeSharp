#pragma once

#include <khook.hpp>
#include <dyncall/dyncall/dyncall_value.h>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace counterstrikesharp {

enum DataType_t
{
    DATA_TYPE_VOID,
    DATA_TYPE_BOOL,
    DATA_TYPE_CHAR,
    DATA_TYPE_UCHAR,
    DATA_TYPE_SHORT,
    DATA_TYPE_USHORT,
    DATA_TYPE_INT,
    DATA_TYPE_UINT,
    DATA_TYPE_LONG,
    DATA_TYPE_ULONG,
    DATA_TYPE_LONG_LONG,
    DATA_TYPE_ULONG_LONG,
    DATA_TYPE_FLOAT,
    DATA_TYPE_DOUBLE,
    DATA_TYPE_POINTER,
    DATA_TYPE_STRING,
    DATA_TYPE_VARIANT
};

// X-macro over the scalar types dynamic functions support: (enum suffix, C++ type, DCValue field, dyncallback suffix, dyncall suffix).
// Variant/aggregate types are rejected; bool goes through Char because DCbool is an int and dyncall only has signed entry points.
#define CSSHARP_NUMERIC_DATA_TYPES(X)                         \
    X(BOOL, bool, B, Char, Char)                              \
    X(CHAR, char, c, Char, Char)                              \
    X(UCHAR, unsigned char, C, UChar, Char)                   \
    X(SHORT, short, s, Short, Short)                          \
    X(USHORT, unsigned short, S, UShort, Short)               \
    X(INT, int, i, Int, Int)                                  \
    X(UINT, unsigned int, I, UInt, Int)                       \
    X(LONG, long, j, Long, Long)                              \
    X(ULONG, unsigned long, J, ULong, Long)                   \
    X(LONG_LONG, long long, l, LongLong, LongLong)            \
    X(ULONG_LONG, unsigned long long, L, ULongLong, LongLong) \
    X(FLOAT, float, f, Float, Float)                          \
    X(DOUBLE, double, d, Double, Double)

#define CSSHARP_POINTER_DATA_TYPES(X)      \
    X(POINTER, void*, p, Pointer, Pointer) \
    X(STRING, const char*, Z, Pointer, Pointer)

#define CSSHARP_SCALAR_DATA_TYPES(X) CSSHARP_NUMERIC_DATA_TYPES(X) CSSHARP_POINTER_DATA_TYPES(X)

// An invocation-local handle exposed to managed DynamicHook. Arguments are
// decoded from the platform ABI by dyncallback, not from a private detour.
class DynamicHookContext
{
  public:
    DynamicHookContext(const std::vector<DataType_t>& types, DataType_t returnType)
        : types(types), returnType(returnType), arguments(types.size())
    {
    }

    template <class T> T getArgument(int index) const { return Read<T>(arguments.at(index)); }
    template <class T> void setArgument(int index, T value)
    {
        Store(arguments.at(index), types.at(index), value);
        argumentsChanged = true;
    }
    template <class T> T getReturnValue() const { return Read<T>(result); }
    template <class T> void setReturnValue(T value)
    {
        Store(result, returnType, value);
        returnChanged = true;
    }

    const std::vector<DataType_t>& types;
    DataType_t returnType;
    std::vector<DCValue> arguments;
    DCValue result{};
    bool argumentsChanged = false;
    bool returnChanged = false;

  private:
    template <class T> static T Read(const DCValue& value)
    {
        static_assert(sizeof(T) <= sizeof(DCValue));
        T result;
        std::memcpy(&result, &value, sizeof(T));
        return result;
    }
    template <class T> static void Store(DCValue& target, DataType_t type, T value)
    {
        target = {};
        if constexpr (std::is_pointer_v<T>)
        {
            if (type != DATA_TYPE_POINTER && type != DATA_TYPE_STRING) throw std::invalid_argument("Hook value is not a pointer");
            target.p = const_cast<void*>(static_cast<const void*>(value));
        }
        else
        {
            switch (type)
            {
#define STORE(E, T, F, N, D)              \
    case DATA_TYPE_##E:                   \
        target.F = static_cast<T>(value); \
        break;
                CSSHARP_NUMERIC_DATA_TYPES(STORE)
#undef STORE
                default:
                    throw std::invalid_argument("Hook value is not numeric");
            }
        }
    }
};

class DynamicHook
{
  public:
    using Handler = std::function<KHook::Action(bool post, DynamicHookContext&)>;
    DynamicHook(void* address, const std::vector<DataType_t>& types, DataType_t returnType, Handler handler);
    ~DynamicHook();
    DynamicHook(const DynamicHook&) = delete;
    DynamicHook& operator=(const DynamicHook&) = delete;
    static void CollectRetired();
    static void ShutdownAll(); // synchronous; call outside hook callbacks

  private:
    struct State;
    std::shared_ptr<State> m_state;
};
} // namespace counterstrikesharp
