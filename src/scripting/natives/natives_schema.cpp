/*
 *  This file is part of CounterStrikeSharp.
 *  CounterStrikeSharp is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  CounterStrikeSharp is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CounterStrikeSharp.  If not, see <https://www.gnu.org/licenses/>. *
 */

#include <ios>
#include <sstream>

#include "scripting/autonative.h"
#include "scripting/script_engine.h"
#include "core/memory.h"
#include "core/log.h"
#include "schema.h"
#include "core/function.h"
#include "core/coreconfig.h"
#include "interfaces/cs2_interfaces.h"
#include <schemasystem.h>

namespace counterstrikesharp {

int16 GetSchemaOffset(ScriptContext& script_context)
{
    auto className = script_context.GetArgument<const char*>(0);
    auto memberName = script_context.GetArgument<const char*>(1);
    auto classKey = hash_32_fnv1a_const(className);
    auto memberKey = hash_32_fnv1a_const(memberName);

    const auto m_key = schema::GetOffset(className, classKey, memberName, memberKey);

    return m_key.offset;
}

bool IsSchemaFieldNetworked(ScriptContext& script_context)
{
    auto className = script_context.GetArgument<const char*>(0);
    auto memberName = script_context.GetArgument<const char*>(1);
    auto classKey = hash_32_fnv1a_const(className);
    auto memberKey = hash_32_fnv1a_const(memberName);

    const auto m_key = schema::GetOffset(className, classKey, memberName, memberKey);

    return m_key.networked;
}

int GetSchemaClassSize(ScriptContext& script_context)
{
    auto className = script_context.GetArgument<const char*>(0);

    CSchemaSystemTypeScope* pType = globals::schemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT);

    SchemaClassInfoData_t* pClassInfo = pType->FindDeclaredClass(className).Get();
    if (!pClassInfo) return -1;

    return pClassInfo->m_nSize;
}

void GetSchemaValueByName(ScriptContext& script_context)
{
    auto instancePointer = script_context.GetArgument<void*>(0);
    auto returnType = script_context.GetArgument<DataType_t>(1);
    auto className = script_context.GetArgument<const char*>(2);
    auto memberName = script_context.GetArgument<const char*>(3);
    auto classKey = hash_32_fnv1a_const(className);
    auto memberKey = hash_32_fnv1a_const(memberName);

    const auto m_key = schema::GetOffset(className, classKey, memberName, memberKey);

    switch (returnType)
    {
#define GET(E, T, F, N, D)                                                                                               \
    case DATA_TYPE_##E:                                                                                                  \
        script_context.SetResult(*reinterpret_cast<std::add_pointer_t<T>>((uintptr_t)(instancePointer) + m_key.offset)); \
        break;
        CSSHARP_NUMERIC_DATA_TYPES(GET)
#undef GET
        case DATA_TYPE_POINTER:
            script_context.SetResult(reinterpret_cast<std::add_pointer_t<void>>((uintptr_t)(instancePointer) + m_key.offset));
            break;
        case DATA_TYPE_STRING:
            script_context.SetResult(reinterpret_cast<std::add_pointer_t<char>>((uintptr_t)(instancePointer) + m_key.offset));
            break;
        default:
            assert(!"Unknown function return type!");
            break;
    }
}

void SetSchemaValueByName(ScriptContext& script_context)
{
    auto instancePointer = script_context.GetArgument<void*>(0);
    auto dataType = script_context.GetArgument<DataType_t>(1);
    auto className = script_context.GetArgument<const char*>(2);
    auto memberName = script_context.GetArgument<const char*>(3);

    if (globals::coreConfig->FollowCS2ServerGuidelines &&
        std::find(schema::CS2BadList.begin(), schema::CS2BadList.end(), memberName) != schema::CS2BadList.end())
    {
        CSSHARP_CORE_ERROR("Cannot set '{}::{}' with \"FollowCS2ServerGuidelines\" option enabled.", className, memberName);
        return;
    }

    auto classKey = hash_32_fnv1a_const(className);
    auto memberKey = hash_32_fnv1a_const(memberName);

    const auto m_key = schema::GetOffset(className, classKey, memberName, memberKey);

    switch (dataType)
    {
#define SET(E, T, F, N, D)                                                                                                        \
    case DATA_TYPE_##E:                                                                                                           \
        *reinterpret_cast<std::add_pointer_t<T>>((uintptr_t)(instancePointer) + m_key.offset) = script_context.GetArgument<T>(4); \
        break;
        CSSHARP_NUMERIC_DATA_TYPES(SET)
#undef SET
        case DATA_TYPE_POINTER:
            *reinterpret_cast<void**>((uintptr_t)(instancePointer) + m_key.offset) = script_context.GetArgument<void*>(4);
            break;
        case DATA_TYPE_STRING:
        {
            auto duplicated = strdup(script_context.GetArgument<const char*>(4));
            *reinterpret_cast<char**>((uintptr_t)(instancePointer) + m_key.offset) = duplicated;
            break;
        }
        default:
            assert(!"Unknown function data type!");
            break;
    }
}

void SchemaSetStateChanged(ScriptContext& script_context)
{
    auto instancePointer = script_context.GetArgument<void*>(0);
    auto offset = script_context.GetArgument<uint32_t>(1);
    auto nArrayIndex = script_context.GetArgument<uint32_t>(2);
    auto nPathIndex = script_context.GetArgument<uint32_t>(3);

    SetStateChanged(reinterpret_cast<uintptr_t>(instancePointer), offset, nArrayIndex, nPathIndex);
}

void SchemaNetworkStateChanged(ScriptContext& script_context)
{
    auto instancePointer = script_context.GetArgument<void*>(0);
    auto offset = script_context.GetArgument<uint32_t>(1);
    auto nArrayIndex = script_context.GetArgument<uint32_t>(2);
    auto nPathIndex = script_context.GetArgument<uint32_t>(3);

    NetworkStateChanged(reinterpret_cast<uintptr_t>(instancePointer), offset, nArrayIndex, nPathIndex);

} // namespace counterstrikesharp

REGISTER_NATIVES(schema, {
    ScriptEngine::RegisterNativeHandler("GET_SCHEMA_OFFSET", GetSchemaOffset);
    ScriptEngine::RegisterNativeHandler("IS_SCHEMA_FIELD_NETWORKED", IsSchemaFieldNetworked);
    ScriptEngine::RegisterNativeHandler("GET_SCHEMA_VALUE_BY_NAME", GetSchemaValueByName);
    ScriptEngine::RegisterNativeHandler("SET_SCHEMA_VALUE_BY_NAME", SetSchemaValueByName);
    ScriptEngine::RegisterNativeHandler("GET_SCHEMA_CLASS_SIZE", GetSchemaClassSize);
    ScriptEngine::RegisterNativeHandler("SCHEMA_SET_STATE_CHANGED", SchemaSetStateChanged);
    ScriptEngine::RegisterNativeHandler("SCHEMA_NETWORK_STATE_CHANGED", SchemaNetworkStateChanged);
})
} // namespace counterstrikesharp
