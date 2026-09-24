#include "core/dynamic_hook.h"
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

#include "mm_plugin.h"
#include "core/timer_system.h"
#include "scripting/autonative.h"
#include "scripting/script_engine.h"
#include "core/function.h"
#include <cassert>

namespace counterstrikesharp {

void DHookGetReturn(ScriptContext& script_context)
{
    auto hook = script_context.GetArgument<DynamicHookContext*>(0);
    auto dataType = script_context.GetArgument<DataType_t>(1);
    if (hook == nullptr)
    {
        script_context.ThrowNativeError("Invalid hook");
        return;
    }

    switch (dataType)
    {
#define GET(E, T, F, N, D)                                   \
    case DATA_TYPE_##E:                                      \
        script_context.SetResult(hook->getReturnValue<T>()); \
        break;
        CSSHARP_SCALAR_DATA_TYPES(GET)
#undef GET
        default:
            script_context.ThrowNativeError("Unsupported dynamic hook type");
            break;
    }
}

void DHookSetReturn(ScriptContext& script_context)
{
    auto hook = script_context.GetArgument<DynamicHookContext*>(0);
    auto dataType = script_context.GetArgument<DataType_t>(1);
    if (hook == nullptr)
    {
        script_context.ThrowNativeError("Invalid hook");
        return;
    }

    auto valueIndex = 2;

    switch (dataType)
    {
#define SET(E, T, F, N, D)                                               \
    case DATA_TYPE_##E:                                                  \
        hook->setReturnValue(script_context.GetArgument<T>(valueIndex)); \
        break;
        CSSHARP_SCALAR_DATA_TYPES(SET)
#undef SET
        default:
            script_context.ThrowNativeError("Unsupported dynamic hook type");
            break;
    }
}

void DHookGetParam(ScriptContext& script_context)
{
    auto hook = script_context.GetArgument<DynamicHookContext*>(0);
    auto dataType = script_context.GetArgument<DataType_t>(1);
    auto paramIndex = script_context.GetArgument<int>(2);
    if (hook == nullptr)
    {
        script_context.ThrowNativeError("Invalid hook");
        return;
    }

    switch (dataType)
    {
#define GET(E, T, F, N, D)                                          \
    case DATA_TYPE_##E:                                             \
        script_context.SetResult(hook->getArgument<T>(paramIndex)); \
        break;
        CSSHARP_SCALAR_DATA_TYPES(GET)
#undef GET
        default:
            script_context.ThrowNativeError("Unsupported dynamic hook type");
            break;
    }
}

void DHookSetParam(ScriptContext& script_context)
{
    auto hook = script_context.GetArgument<DynamicHookContext*>(0);
    auto dataType = script_context.GetArgument<DataType_t>(1);
    auto paramIndex = script_context.GetArgument<int>(2);
    if (hook == nullptr)
    {
        script_context.ThrowNativeError("Invalid hook");
        return;
    }

    auto valueIndex = 3;

    switch (dataType)
    {
#define SET(E, T, F, N, D)                                                        \
    case DATA_TYPE_##E:                                                           \
        hook->setArgument(paramIndex, script_context.GetArgument<T>(valueIndex)); \
        break;
        CSSHARP_SCALAR_DATA_TYPES(SET)
#undef SET
        default:
            script_context.ThrowNativeError("Unsupported dynamic hook type");
            break;
    }
}

template <void (*Handler)(ScriptContext&)> void CheckedDynamicHookNative(ScriptContext& context)
{
    try
    {
        Handler(context);
    }
    catch (const std::exception& exception)
    {
        context.ThrowNativeError("%s", exception.what());
    }
}

REGISTER_NATIVES(dynamichooks, {
    ScriptEngine::RegisterNativeHandler("DYNAMIC_HOOK_GET_RETURN", CheckedDynamicHookNative<DHookGetReturn>);
    ScriptEngine::RegisterNativeHandler("DYNAMIC_HOOK_SET_RETURN", CheckedDynamicHookNative<DHookSetReturn>);
    ScriptEngine::RegisterNativeHandler("DYNAMIC_HOOK_GET_PARAM", CheckedDynamicHookNative<DHookGetParam>);
    ScriptEngine::RegisterNativeHandler("DYNAMIC_HOOK_SET_PARAM", CheckedDynamicHookNative<DHookSetParam>);
})
} // namespace counterstrikesharp
