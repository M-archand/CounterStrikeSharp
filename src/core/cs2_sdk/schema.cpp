/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "schema.h"

#include "interfaces/cs2_interfaces.h"
#include "core/globals.h"
#include "core/gameconfig.h"
#include "core/memory.h"
#include "core/log.h"

#include "tier0/utlmap.h"
#include <thread>
#include <schemasystem/schemasystem.h>
#include <entity2/entitysystem.h>
#include <entity2/entityclass.h>
#include <networksystem/inetworkserializer.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using SchemaKeyValueMap_t = CUtlOrderedMap<uint32_t, SchemaKey>;
using SchemaTableMap_t = CUtlOrderedMap<uint32_t, SchemaKeyValueMap_t*>;

// Classes whose table was built before the serializer database existed, their networked flags are resolved on a later lookup
struct PendingNetworkedClass_t
{
    uint32_t classKey;
    SchemaClassInfoData_t* pClassInfo;
};

static SchemaTableMap_t g_schemaTableMap;
static std::vector<PendingNetworkedClass_t> g_pendingNetworkedClasses;

static CNetworkSerializerCodeGenDatabase* GetNetworkSerializerDatabase()
{
    if (!GameEntitySystem()) return nullptr;

    CEntityClass* pEntityClass = GameEntitySystem()->FindClassByName("CBaseEntity");
    if (!pEntityClass || !pEntityClass->m_NetworkSerializerInfo) return nullptr;

    return pEntityClass->m_NetworkSerializerInfo->m_pDatabase;
}

static CNetworkSerializerClassInfo* FindNetworkSerializerClassInfo(CNetworkSerializerCodeGenDatabase* pDatabase, const char* className)
{
    if (!pDatabase) return nullptr;

    auto index = pDatabase->m_ClassInfos.Find(className);
    if (index == pDatabase->m_ClassInfos.InvalidIndex()) return nullptr;

    return pDatabase->m_ClassInfos[index];
}

static bool IsFieldNetworked(CNetworkSerializerClassInfo* pNetworkClassInfo, SchemaClassFieldData_t& field)
{
    if (!pNetworkClassInfo) return false;
    return pNetworkClassInfo->FindField(field.m_pName) != nullptr;
}

static void ResolveNetworkedFlags()
{
    if (g_pendingNetworkedClasses.empty()) return;

    // GET_SCHEMA_OFFSET may run off the game thread, only the game thread mutates the cached flags
    if (counterstrikesharp::globals::gameThreadId != std::this_thread::get_id()) return;

    CNetworkSerializerCodeGenDatabase* pDatabase = GetNetworkSerializerDatabase();
    if (!pDatabase) return;

    for (const PendingNetworkedClass_t& pending : g_pendingNetworkedClasses)
    {
        auto tableMapIndex = g_schemaTableMap.Find(pending.classKey);
        if (!g_schemaTableMap.IsValidIndex(tableMapIndex)) continue;

        SchemaKeyValueMap_t* keyValueMap = g_schemaTableMap[tableMapIndex];
        CNetworkSerializerClassInfo* pNetworkClassInfo = FindNetworkSerializerClassInfo(pDatabase, pending.pClassInfo->m_pName);

        for (int i = 0; i < pending.pClassInfo->m_nNumFields; ++i)
        {
            SchemaClassFieldData_t& field = pending.pClassInfo->m_pFields[i];
            auto memberIndex = keyValueMap->Find(hash_32_fnv1a_const(field.m_pName));
            if (keyValueMap->IsValidIndex(memberIndex))
                keyValueMap->Element(memberIndex).networked = IsFieldNetworked(pNetworkClassInfo, field);
        }
    }

    g_pendingNetworkedClasses.clear();
}

static bool InitSchemaFieldsForClass(SchemaTableMap_t* tableMap, const char* className, uint32_t classKey)
{
    CSchemaSystemTypeScope* pType = counterstrikesharp::globals::schemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT);

    if (!pType) return false;

    SchemaClassInfoData_t* pClassInfo = pType->FindDeclaredClass(className).Get();

    if (!pClassInfo)
    {
        SchemaKeyValueMap_t* map = new SchemaKeyValueMap_t();
        tableMap->Insert(classKey, map);

        Warning("InitSchemaFieldsForClass(): '%s' was not found!\n", className);
        return false;
    }

    const auto fieldsSize = pClassInfo->m_nNumFields;
    SchemaClassFieldData_t* pFields = pClassInfo->m_pFields;

    CNetworkSerializerCodeGenDatabase* pDatabase = GetNetworkSerializerDatabase();
    CNetworkSerializerClassInfo* pNetworkClassInfo = FindNetworkSerializerClassInfo(pDatabase, className);

    if (!pDatabase)
    {
        g_pendingNetworkedClasses.push_back({ classKey, pClassInfo });
        Warning("InitSchemaFieldsForClass(): '%s' was looked up before the entity system exists, networked flags will be resolved later\n",
                className);
    }

    SchemaKeyValueMap_t* keyValueMap = new SchemaKeyValueMap_t();
    keyValueMap->EnsureCapacity(fieldsSize);
    tableMap->Insert(classKey, keyValueMap);

    for (int i = 0; i < fieldsSize; ++i)
    {
        SchemaClassFieldData_t& field = pFields[i];

        if (field.m_pType->m_eTypeCategory == SCHEMA_TYPE_ATOMIC && field.m_pType->m_eAtomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T)
            keyValueMap->Insert(hash_32_fnv1a_const(field.m_pName),
                                { field.m_nSingleInheritanceOffset, IsFieldNetworked(pNetworkClassInfo, field),
                                  static_cast<CSchemaType_Atomic_CollectionOfT*>(field.m_pType)->m_pfnManipulator });
        else
            keyValueMap->Insert(hash_32_fnv1a_const(field.m_pName),
                                { field.m_nSingleInheritanceOffset, IsFieldNetworked(pNetworkClassInfo, field) });
    }

    return true;
}

int16_t schema::FindChainOffset(const char* className)
{
    CSchemaSystemTypeScope* pType = counterstrikesharp::globals::schemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT);

    if (!pType) return false;

    auto* pClassInfo = pType->FindDeclaredClass(className).Get();

    do
    {
        SchemaClassFieldData_t* pFields = pClassInfo->m_pFields;
        const auto fieldsSize = pClassInfo->m_nNumFields;
        for (int i = 0; i < fieldsSize; ++i)
        {
            SchemaClassFieldData_t& field = pFields[i];

            if (V_strcmp(field.m_pName, "__m_pChainEntity") == 0)
            {
                return field.m_nSingleInheritanceOffset;
            }
        }
    } while ((pClassInfo = pClassInfo->m_pBaseClasses->m_pClass) != nullptr);

    return 0;
}

SchemaKey schema::GetOffset(const char* className, uint32_t classKey, const char* memberName, uint32_t memberKey)
{
    ResolveNetworkedFlags();

    auto tableMapIndex = g_schemaTableMap.Find(classKey);
    if (!g_schemaTableMap.IsValidIndex(tableMapIndex))
    {
        if (InitSchemaFieldsForClass(&g_schemaTableMap, className, classKey)) return GetOffset(className, classKey, memberName, memberKey);

        return { 0, 0 };
    }

    SchemaKeyValueMap_t* tableMap = g_schemaTableMap[tableMapIndex];
    auto memberIndex = tableMap->Find(memberKey);
    if (!tableMap->IsValidIndex(memberIndex))
    {
        return { 0, 0 };
    }

    return tableMap->Element(memberIndex);
}

void NetworkStateChanged(uintptr_t chainEntity, uint32_t offset, uint32_t nArrayIndex, uint32_t nPathIndex)
{
    CNetworkStateChangedInfo info(offset, nArrayIndex, nPathIndex);

    if (counterstrikesharp::globals::NetworkStateChanged)
        counterstrikesharp::globals::NetworkStateChanged(reinterpret_cast<void*>(chainEntity), info);
}

void SetStateChanged(uintptr_t pEntity, uint32_t offset, uint32_t nArrayIndex, uint32_t nPathIndex)
{
    CNetworkStateChangedInfo info(offset, nArrayIndex, nPathIndex);

    static auto fnOffset = counterstrikesharp::globals::gameConfig->GetOffset("SetStateChanged");
    CALL_VIRTUAL(void, fnOffset, (void*)pEntity, &info);
}
