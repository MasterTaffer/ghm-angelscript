#include <algorithm>
#include <cassert>
#include <cstring>
#include "entity.h"
#include "stringutils.h"

namespace ASECS
{

const int EntityEventInitId = 0;
const int EntityEventDeinitId = 1;

EntitySystem::EntitySystem(EntitySystemManager *esm, asIScriptEngine *eng)
{
    engine = eng;
    manager = esm;
    entitiesToKill.reserve(20000);
    entitiesToSpawn.reserve(20000);
    entitiesToKillSwap.reserve(20000);
    entitiesToSpawnSwap.reserve(20000);
}

EntitySystem::~EntitySystem()
{
    clear();
}

void EntitySystem::invalidateIterators()
{
    for (auto* i : activeComponentIterators)
    {
        i->invalidated = true;
        i->finished = true;
    }
    for (auto* i : activeEntityIterators)
    {
        i->invalidated = true;
        i->finished = true;
    }
}

void EntitySystem::clearPreparedEvents()
{
    for (auto& r : preparedGlobalEvents)
    {
        r.event->Release();
    }
    preparedGlobalEvents.clear();
    for (auto& r : preparedLocalEvents)
    {
        r.first->release();
        r.second.event->Release();
    }
    preparedLocalEvents.clear();
}

void EntitySystem::prepareGlobalEvent(asIScriptObject * o, int id)
{
    if (o == nullptr)
        return;


    if ((id & asTYPEID_SCRIPTOBJECT) == 0 || (id & asTYPEID_OBJHANDLE) != 0)
    {
        auto* ctx = asGetActiveContext();
        if (ctx)
        {
            ctx->SetException("ESM::QueueGlobalEvent called with illegal arguments");
            return;
        }
    }
    o->AddRef();
    preparedGlobalEvents.push_back({ (unsigned int) id & asTYPEID_MASK_SEQNBR, o });
}

void EntitySystem::prepareLocalEvent(Entity * e, asIScriptObject* o, int id)
{
    if (o == nullptr || e == nullptr)
        return;


    if ((id & asTYPEID_SCRIPTOBJECT) == 0 || (id & asTYPEID_OBJHANDLE) != 0)
    {
        auto* ctx = asGetActiveContext();
        if (ctx)
        {
            ctx->SetException("ESM::QueueGlobalEvent called with illegal arguments");
            return;
        }
    }

    e->addRef();
    o->AddRef();
    preparedLocalEvents.push_back({ e, { (unsigned int)id & asTYPEID_MASK_SEQNBR, o} });
}

bool EntitySystem::sendEvents()
{
    if (preparedGlobalEventsSwap.size() > 0 || preparedLocalEventsSwap.size() > 0)
    {
        auto* ctx = asGetActiveContext();
        if (ctx)
            ctx->SetException("Recursive ESM::SendEventss call");
        return false;
    }

    auto* ctx = engine->RequestContext();

    std::swap(preparedGlobalEvents, preparedGlobalEventsSwap);
    for (auto& r : preparedGlobalEventsSwap)
    {
        ++stat_globalEventsSent;
        //TODO: optimize?
        //Use an entityiterator just to check for iterator invalidation
        //somebody abuses
        EntityIterator* ei = constructEntityIterator();
        auto it = componentsByEvent.find(r.id);

        if (it != componentsByEvent.end())
        for (std::pair<Component*, unsigned int>& p : it->second)
        {
            auto* c = p.first;
                
            auto* obj = c->object;
            if (obj == nullptr)
                continue;
            auto* func = c->componentClass->eventHandlers[p.second].second;

            ctx->Prepare(func);
            ctx->SetObject(obj);
            ctx->SetArgAddress(0, r.event);
            ctx->Execute();

            if (ei->invalidated)
            {
                //use the inbuilt exception system in entity iterator
                //to write the exception
                ei->next();
                break;
            }
        }

        r.event->Release();
        releaseEntityIterator(ei);
    }
    preparedGlobalEventsSwap.clear();

    std::swap(preparedLocalEvents, preparedLocalEventsSwap);
    for (auto& r : preparedLocalEventsSwap)
    {
        ++stat_localEventsSent;
        if (r.first->dead == false)
        {
            r.first->sendEventNowInContext(r.second.event, r.second.id, ctx);
        }
        r.first->release();
        r.second.event->Release();
    }
    preparedLocalEventsSwap.clear();

    engine->ReturnContext(ctx);
    return (preparedGlobalEvents.size() > 0 || preparedLocalEvents.size() > 0);
}

void EntitySystem::cleanUp()
{
    invalidateIterators();

    {
        auto it = componentsByClass.begin();
        while (it != componentsByClass.end())
        {
            std::vector<Component*>& cv = it->second;
            cv.erase(std::remove_if(cv.begin(), cv.end(), [&]
            (Component* c) {
                if (c->dead && (c->entity->reused == false))
                {
                    return true;
                }
                return false;
            }), cv.end());

            if (cv.size() == 0)
            {
                ++it;
                //it = componentsByClass.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    {
        auto it = componentsByEvent.begin();
        while (it != componentsByEvent.end())
        {
            std::vector<std::pair<Component*, unsigned int>>& cv = it->second;
            cv.erase(std::remove_if(cv.begin(), cv.end(), [&]
            (const std::pair<Component*, unsigned int>& p) {
                if (p.first->dead && (p.first->entity->reused == false))
                {
                    return true;
                }
                return false;
            }), cv.end());

            ++it;
        }
    }


    allEntities.erase(std::remove_if(allEntities.begin(), allEntities.end(), [&]
    (Entity* e){
        if (e->dead && (e->reused == false))
        {
            e->release();
            return true;
        }
        return false;
    }), allEntities.end());

    deadEntitiesByTypeHash.clear();
}

void EntitySystem::updateEntityLists()
{
    if (entitiesToSpawnSwap.size() > 0 || entitiesToKillSwap.size() > 0)
    {
        auto* ctx = asGetActiveContext();
        if (ctx)
            ctx->SetException("Recursive ESM::UpdateEntityLists call");
        return;
    }
    //manager->log("List update - Killing  ", entitiesToKill.size(), " entities");

    asIScriptContext* ctx = engine->RequestContext();
    
    //If some abusers spawn entities or kill entities during initialization
    while (entitiesToKill.size() > 0 || entitiesToSpawn.size() > 0)
    {
        //Is there any reason to kill entities before spawning them?
        std::swap(entitiesToSpawn, entitiesToSpawnSwap);
        for (Entity* e : entitiesToSpawnSwap)
        {
            if (!(e->reused))
            {
                allEntities.push_back(e);
                for (Component& c : e->components)
                {
                    auto it = componentsByClass.find(c.componentClass->id);
                    if (it == componentsByClass.end())
                    {
                        
                    }
                    else
                        it->second.push_back(&c);

                    unsigned int index = 0;
                    for (auto& evh : c.componentClass->eventHandlers)
                    {
                        auto it = componentsByEvent.find(evh.first);
                        std::vector<std::pair<Component*, unsigned int>>* p;
                        if (it == componentsByEvent.end())
                        {
                            componentsByEvent[evh.first] = std::vector<std::pair<Component*, unsigned int>>();
                            p = &(componentsByEvent[evh.first]);
                        }
                        else
                            p = &(it->second);
                        p->push_back({ &c, index });
                        ++index;
                    }
                }
            }
            e->setDead(false);
            e->reused = false;
        }
        //if some abuser uses component iterators in the init/deinit, break em
        invalidateIterators();
        for (Entity* e : entitiesToSpawnSwap)
            e->sendSpecialEventNowInContext(EntityEventInitId, ctx);
        
        entitiesToSpawnSwap.clear();


        //steal datas
        std::swap(entitiesToKill, entitiesToKillSwap);
        for (Entity* e : entitiesToKillSwap)
        {
            if (e->dead)
                continue;
            e->sendSpecialEventNowInContext(EntityEventDeinitId, ctx);
            e->setDead(true);

            /*
            if (!e->type->hasCollisions)
            {
                std::vector<Entity*>* vec;
                auto it = deadEntitiesByTypeHash.find(e->type->hash);
                if (it == deadEntitiesByTypeHash.end())
                {
                    vec = new std::vector<Entity*>();
                    deadEntitiesByTypeHash[e->type->hash] = std::unique_ptr<std::vector<Entity*>>(vec);
                }
                else
                    vec = it->second.get();
                vec->push_back(e);
            }
            */

        }
        entitiesToKillSwap.clear();
    
    }
    engine->ReturnContext(ctx);
    cleanUp();
}

Entity* EntitySystem::constructEntity(const EntityType * type)
{
    ++stat_entityConstructions;
    if (!type->hasCollisions)
    {
        auto it = deadEntitiesByTypeHash.find(type->hash);
        if (it != deadEntitiesByTypeHash.end())
        {
            
            auto& vec = *(it->second);
            size_t index = 0;
            size_t vsize = vec.size();
            for (Entity* b : vec)
            {
                //EntitySystem hold the only reference
                if (b->getRefCount() == 1)
                {
                    //swap with last element
                    if (index != vsize - 1)
                    {
                        vec[index] = std::move(vec[vsize - 1]);
                    }
                    //and resize
                    vec.resize(vsize - 1);

                    b->reused = true;
                    b->id = getNextEntityId();
                    buildEntityComponents(b);
                    buildEntityComponentReferences(b, type);
                    entitiesToSpawn.push_back(b);
                    b->addRef();
                    return b;
                }
                index++;
            }
            
        }
    }

    //The only place where entities are construced

    Entity* n = new Entity();
    n->system = this;
    n->type = type;
    n->components.reserve(type->componentTypes.size());
    n->id = getNextEntityId();
    for (ComponentClass* c : type->componentTypes)
    {
        n->components.push_back(Component(c, n));
    }
    buildEntityComponents(n);
    buildEntityComponentReferences(n, type);
    entitiesToSpawn.push_back(n);

    n->addRef();
    return n;
}

Entity * EntitySystem::constructEntity(unsigned int moldId)
{
    EntityType* type = manager->getTypeByMoldId(moldId);
    if (type == nullptr)
        return nullptr;
    Entity* e =  constructEntity(type);
    return e;
}

void EntitySystem::killAllEntities()
{
    for (Entity* e : entitiesToSpawn)
        killEntity(e);
    for (Entity* e : allEntities)
        killEntity(e);
}

void EntitySystem::killEntity(Entity * e)
{
    entitiesToKill.push_back(e);
}

void EntitySystem::clear()
{
    clearPreparedEvents();

    for (Entity* e : entitiesToSpawn)
    {
        e->setDead(true);
        e->release();
    }

    for (Entity* e : allEntities)
    {
        e->setDead(true);
        e->release();
    }

    allEntities.clear();
    entitiesToSpawn.clear();
    entitiesToKill.clear();
    componentsByEvent.clear();
    deadEntitiesByTypeHash.clear();
    componentsByClass.clear();
    componentsByEvent.clear();

    stat_entityIteratorsConstructed = 0;
    stat_componentIteratorsConstructed = 0;
    stat_entityConstructions = 0;
    stat_globalEventsSent = 0;
    stat_localEventsSent = 0;
    lastEntityId = 0;
}

ComponentIterator * EntitySystem::constructComponentIterator(asITypeInfo * type)
{
    ++stat_componentIteratorsConstructed;

    auto it = componentsByClass.find(type->GetSubType()->GetTypeId() & asTYPEID_MASK_SEQNBR);
    ComponentIterator* ci;
    if (it == componentsByClass.end())
    {
        ci = new ComponentIterator(this, nullptr);
    }
    else
    {
        ci = new ComponentIterator(this, &(it->second));
    }
    activeComponentIterators.insert(ci);
    return ci;
}

void EntitySystem::releaseComponentIterator(ComponentIterator * ci)
{
    if (ci)
    {
        activeComponentIterators.erase(ci);
        delete ci;
    }
}

EntityIterator * EntitySystem::constructEntityIterator()
{
    ++stat_entityIteratorsConstructed;
    EntityIterator* ei;
    ei = new EntityIterator(this, &allEntities);
    activeEntityIterators.insert(ei);
    return ei;
}

void EntitySystem::releaseEntityIterator(EntityIterator * ei)
{
    if (ei)
    {
        activeEntityIterators.erase(ei);
        delete ei;
    }
}

void EntitySystem::logDebugInfo()
{
    manager->log(EntitySystemManager::Info, "EntitySystem::logDebugData");
    manager->log(EntitySystemManager::Info, "	All Entities Count: ", allEntities.size());
    manager->log(EntitySystemManager::Info, "	Entities To Spawn: ", entitiesToSpawn.size());
    manager->log(EntitySystemManager::Info, "	Entities To Kill: ", entitiesToKill.size());
    manager->log(EntitySystemManager::Info, "	Active Component Classes: ", componentsByClass.size());
    manager->log(EntitySystemManager::Info, "	Active Component Classes By Event: ", componentsByEvent.size());
    
    manager->log(EntitySystemManager::Info, "	Dead Entity Classes: ", deadEntitiesByTypeHash.size());
    size_t totc = 0;
    for (auto& r : deadEntitiesByTypeHash)
        totc += r.second->size();
    manager->log(EntitySystemManager::Info, "	Total Dead Entities: ", totc);
    manager->log(EntitySystemManager::Info, "	Entity Mold Count: ", manager->entityMolds.size());

    manager->log(EntitySystemManager::Info, "	Global events sent: ", stat_globalEventsSent);
    manager->log(EntitySystemManager::Info, "	Local events sent: ", stat_localEventsSent);
    manager->log(EntitySystemManager::Info, "	Entities constructed: ", stat_entityConstructions);
    manager->log(EntitySystemManager::Info, "	Component iterators constructed: ", stat_componentIteratorsConstructed);
    manager->log(EntitySystemManager::Info, "	Entity iterators constructed: ", stat_entityIteratorsConstructed);
}

void EntitySystem::preallocate()
{
    for (auto& m : manager->classes)
    {
        auto it = componentsByClass.find(m.first);
        if (it == componentsByClass.end())
        {
            auto vec = std::vector<Component*>();
            vec.reserve(1000);
            componentsByClass[m.first] = std::move(vec);
        }
    }
}


void EntitySystem::buildEntityComponents(Entity* entity)
{
    asIScriptContext* ctx = engine->RequestContext();
    for (auto& component : entity->components)
    {
        component.object = nullptr;
        ctx->Prepare(component.componentClass->factory);
        int res = ctx->Execute();
        if (res != asEXECUTION_FINISHED)
        {
            manager->log(EntitySystemManager::Warning, "Failed to initialize component: ", ctx->GetExceptionString());
        }
        else
        {
            component.object = *(asIScriptObject**)ctx->GetAddressOfReturnValue();
            component.object->AddRef();
        }
    }
    
    engine->ReturnContext(ctx);

}

void EntitySystem::buildEntityComponentReferences(Entity * entity, const EntityType * type)
{
    for (auto& c : entity->components)
    {
        if (c.componentClass->entityReference.has)
        {
            entity->addRef();
            Entity** ptrTo = (Entity**)(((char*)c.object) + c.componentClass->entityReference.offset);
            if (*ptrTo != nullptr)
                (*ptrTo)->release();
            (*ptrTo) = entity;
        }
    }
    for (auto& ecr : type->componentReferences)
    {
        Component& c = entity->components[ecr.componentIndex];
        if (c.object)
        {
            auto* target = entity->components[ecr.toComponent].object;
            if (target != nullptr)
                target->AddRef();

            asIScriptObject** ptrTo = (asIScriptObject**)(((char*)c.object) + ecr.referenceOffset);
            if (*ptrTo != nullptr)
                (*ptrTo)->Release();
            (*ptrTo) = target;
        }
    }
    
}


EntityType* EntitySystemManager::entityMoldFactory(uint32_t* list)
{
    uint32_t cnt = *list;
    ++list;
    std::vector<uint32_t> vec;
    for (uint32_t i = 0; i < cnt; i++)
    {
        vec.push_back(*list);
        ++list;
    }
    int i = getMoldId(vec);
    if (i >= 0)
    {
        EntityType* t = getTypeByMoldId(i);
        return t;
    }
    return nullptr;
}



void ComponentInfoConstruct(asITypeInfo* ti, void* v)
{
    auto casted = static_cast<unsigned int*>(v);
    *casted = ti->GetSubType()->GetTypeId() & asTYPEID_MASK_SEQNBR;
}

void ComponentInfoDestruct(void* v)
{
}

unsigned int ComponentInfoGet(unsigned int* v)
{
    return *v;
}

void EntitySystemManager::registerEngine(asIScriptEngine* ase)
{
    ase->AddRef();
    this->engine = ase;
    this->system = std::unique_ptr<EntitySystem>(new EntitySystem(this, ase));

    int r = ase->RegisterObjectType("Entity", 0, asOBJ_REF);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Entity", asBEHAVE_ADDREF, "void f()", asMETHOD(Entity, addRef), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Entity", asBEHAVE_RELEASE, "void f()", asMETHOD(Entity, release), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectProperty("Entity", "const uint id", asOFFSET(Entity, id));
    assert(r >= 0);

    r = ase->RegisterObjectProperty("Entity", "const bool dead", asOFFSET(Entity, dead));
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Entity", "bool opEquals(Entity&)", asMETHOD(Entity, equals), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Entity", "bool getComponent(?&out)", asMETHOD(Entity, getComponentObject), asCALL_THISCALL);
    assert(r >= 0);


    r = ase->RegisterObjectMethod("Entity", "uint sendEventNow(?&in)", asMETHOD(Entity, sendEventNow), asCALL_THISCALL);
    assert(r >= 0);


    r = engine->RegisterObjectType("EntityMold", 0, asOBJ_REF | asOBJ_NOCOUNT);
    assert(r >= 0);

    r = engine->RegisterObjectBehaviour("EntityMold", asBEHAVE_LIST_FACTORY, "EntityMold@ f(int & in) {repeat int}", asMETHOD(EntitySystemManager, entityMoldFactory), asCALL_THISCALL_ASGLOBAL, this);
    assert(r >= 0);



    r = ase->SetDefaultNamespace("ESM");
    assert(r >= 0);

    /*
    r = ase->RegisterGlobalFunction("int GetMoldId(uint[]&)", asMETHODPR(EntitySystemManager, getMoldId, (CScriptArray*), int), asCALL_THISCALL_ASGLOBAL, this);
    assert(r >= 0);
    r = ase->RegisterGlobalFunction("Entity@ ConstructEntity(uint)", asMETHODPR(EntitySystem, constructEntity, (unsigned int), Entity*), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);
    */

    r = ase->RegisterGlobalFunction("Entity@ ConstructEntity(const EntityMold &)", asMETHODPR(EntitySystem, constructEntity, (const EntityType*), Entity*), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);


    r = ase->RegisterGlobalFunction("void KillEntity(Entity&)", asMETHOD(EntitySystem, killEntity), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("void KillAllEntities()", asMETHOD(EntitySystem, killAllEntities), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);


    r = ase->RegisterGlobalFunction("void CleanUp()", asMETHOD(EntitySystem, cleanUp), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("void UpdateEntityLists()", asMETHOD(EntitySystem, updateEntityLists), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("bool SendEvents()", asMETHOD(EntitySystem, sendEvents), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("void QueueLocalEvent(Entity&, ?&in)", asMETHOD(EntitySystem, prepareLocalEvent), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("void QueueGlobalEvent(?&in)", asMETHOD(EntitySystem, prepareGlobalEvent), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("void LogDebugInfo()", asMETHOD(EntitySystem, logDebugInfo), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = ase->SetDefaultNamespace("");
    assert(r >= 0);

    r = engine->RegisterObjectType("ComponentIterator<class T>", 0, asOBJ_REF | asOBJ_SCOPED | asOBJ_TEMPLATE);
    assert(r >= 0);

    r = engine->RegisterObjectBehaviour("ComponentIterator<T>", asBEHAVE_FACTORY, "ComponentIterator<T> @f(int&in)", asMETHOD(EntitySystem, constructComponentIterator), asCALL_THISCALL_ASGLOBAL, this->system.get());
    assert(r >= 0);

    r = engine->RegisterObjectBehaviour("ComponentIterator<T>", asBEHAVE_RELEASE, "void f()", asMETHOD(ComponentIterator, release), asCALL_THISCALL);
    assert(r >= 0);

    r = engine->RegisterObjectMethod("ComponentIterator<T>", "T@ next()", asMETHOD(ComponentIterator, next), asCALL_THISCALL);
    assert(r >= 0);

    r = engine->RegisterObjectType("ComponentInfo<class T>", sizeof(unsigned int), asOBJ_VALUE | asOBJ_TEMPLATE | asGetTypeTraits<unsigned int>());
    assert(r >= 0);

    r = engine->RegisterObjectBehaviour("ComponentInfo<T>", asBEHAVE_CONSTRUCT, "void f(int&in)", asFUNCTION(ComponentInfoConstruct), asCALL_CDECL_OBJLAST);
    assert(r >= 0);

    r = engine->RegisterObjectBehaviour("ComponentInfo<T>", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(ComponentInfoDestruct), asCALL_CDECL_OBJLAST);
    assert(r >= 0);

    r = engine->RegisterObjectMethod("ComponentInfo<T>", "uint getId()", asFUNCTION(ComponentInfoGet), asCALL_CDECL_OBJLAST);
    assert(r >= 0);

    entityTypeInfo = ase->GetTypeInfoByDecl("Entity");
    entityTypeInfo->AddRef();
    
#ifdef NDEBUG
    (void)(r);
#endif
}


EntitySystem* EntitySystemManager::getSystem()
{
    return system.get();
}

int EntitySystemManager::getMoldId(const std::vector<uint32_t>& invec)
{
    auto vec = invec;
    std::sort(vec.begin(), vec.end());
    std::vector<ComponentClass*> cv;
    cv.reserve(vec.size());
    unsigned int lastId = 0;
    for (auto val : vec)
    {
        if (val == lastId)
        {
            auto* ctx = asGetActiveContext();
            if (ctx)
                ctx->SetException("EntitySystemManager::getMoldId invalid mold construction");
            return -1;
        }
        lastId = val;
        auto it = classes.find(val);
        if (it == classes.end())
        {
            auto* ctx = asGetActiveContext();
            if (ctx)
                ctx->SetException("EntitySystemManager::getMoldId must be called with registered component IDs");
            return -1;
        }
        cv.push_back(it->second.get());
    }

    uint32_t hash = 5381;
    for (auto it = vec.begin(); it != vec.end(); it++)
    {
        hash = hash * 33;
        hash += *it;
    }
    auto it = moldIdsByHash.find(hash);
    if (it != moldIdsByHash.end())
    {
        auto* cto = entityMolds[it->second].get();
        if (vec.size() == cto->componentTypes.size())
        {
            bool allEq = true;
            for (size_t i = 0; i < vec.size(); i++)
            {
                if (vec[i] != cto->componentTypes[i]->id)
                {
                    allEq = false;
                    break;
                }
            }
            if (allEq)
            {
                return it->second;
            }
        }
        cto->hasCollisions = true;
        log(EntitySystemManager::Warning, "EntitySystemManager MoldCollision");
    }
    EntityType* et = new EntityType;
    et->componentTypes = std::move(cv);
    et->hasCollisions = false;
    et->hash = hash;




    entityMolds.push_back(std::unique_ptr<EntityType>(et));

    //Precalculate valid component referencess

    //pretty hairy, but gotta do what ya gotta do
    for (unsigned int i = 0; i < et->componentTypes.size(); i++)
    {

        for (unsigned int i2 = 0; i2 < et->componentTypes.size(); i2++)
        {
            if (i == i2)
                continue;

            auto* c = et->componentTypes[i];
            auto* c2 = et->componentTypes[i2];

            for (unsigned int ri = 0; ri < c->componentReferences.size(); ri++)
            {
                auto& cr = c->componentReferences[ri];
                if (cr.second.has && cr.first == c2->id)
                {
                    EntityType::EntityComponentReference ecr;
                    ecr.componentIndex = i;
                    ecr.referenceOffset = cr.second.offset;
                    ecr.toComponent = i2;
                    et->componentReferences.push_back(ecr);
                }
            }

        }
    }


    //Precalculate event handlers

    for (unsigned int i = 0; i < et->componentTypes.size(); i++)
    {
        auto* c = et->componentTypes[i];
        for (unsigned int j = 0; j < c->eventHandlers.size(); j++)
        {
            auto& p = c->eventHandlers[j];
            et->eventHandlers[p.first].push_back({ i, j });
        }
    }



    auto index = entityMolds.size() - 1;
    moldIdsByHash[hash] = index;

    return entityMolds.size() - 1;
}

int EntitySystemManager::getMoldId(CScriptArray* arr)
{
    if (arr->GetElementTypeId() != asTYPEID_UINT32)
    {
        auto* ctx = asGetActiveContext();
        if (ctx)
        ctx->SetException("EntitySystemManager::getMoldId array element type not uint32");
        return -1;
    }
    std::vector<uint32_t> vec;
    vec.reserve(arr->GetSize());
    for (unsigned int i = 0; i < arr->GetSize(); i++)
    {
        vec.push_back(static_cast<unsigned int*>(arr->GetBuffer())[i]);
    }
    return getMoldId(vec);
}


void EntitySystemManager::initEntityClasses(CScriptBuilder* builder)
{
    asIScriptModule* mod = builder->GetModule();
    unsigned int cnt = mod->GetObjectTypeCount();
    for (unsigned int a = 0; a < cnt; ++a)
    {
        asITypeInfo* ti = mod->GetObjectTypeByIndex(a);
        unsigned int tid = ti->GetTypeId();
        auto metadata = SplitStringByComma(builder->GetMetadataStringForType(tid));
        if (IsPresentInList(metadata,"Component"))
        {
            if (classes.find(tid) != classes.end())
            {
                log(EntitySystemManager::Warning, "Duplicate ComponentClass TypeId: ", ti->GetName(), " tid ", tid);
                continue;
            }
            else
            {
                unsigned int fcnt = ti->GetFactoryCount();
                asIScriptFunction* ourfact = nullptr;
                for (unsigned int f = 0; f < fcnt; f++)
                {
                    asIScriptFunction* fact = ti->GetFactoryByIndex(f);
                    if (fact->GetParamCount() != 0)
                        continue;
                    ourfact = fact;
                    break;
                }
                if (ourfact == nullptr)
                {
                    log(EntitySystemManager::Warning, "ComponentClass with no default constructor: ", ti->GetName());
                    continue;
                }
                ComponentClass* c = new ComponentClass(ti->GetName(), ourfact, ti);
                tid = tid & asTYPEID_MASK_SEQNBR;

                c->id = tid;
                classes[tid] = std::unique_ptr<ComponentClass>(c);
                log(EntitySystemManager::Info, "ComponentClass: ", ti->GetName(), " ", tid);
                
            }
        }
    }
    for (auto& pair : classes)
    {
        auto* cls = pair.second.get();
        auto* ti = cls->typeInfo;
        std::string className = ti->GetName();
        while (ti)
        {
            auto ctid = ti->GetTypeId();
            //Iterate methods
            auto mcnt = ti->GetMethodCount();
            for (unsigned int i = 0; i < mcnt; i++)
            {
                auto func = ti->GetMethodByIndex(i);
                const char* name = func->GetName();
                auto metadata = SplitStringByComma(builder->GetMetadataStringForTypeMethod(ctid, func));
                
                if (IsPresentInList(metadata, "EventHandler"))
                {
                    if (func->GetParamCount() != 1)
                    {
                        log(EntitySystemManager::Warning, "Invalid EventHandler: ", className, "::", name, ", illegal parameter count");
                        continue;
                    }

                    if (func->GetReturnTypeId() != 0)
                    {
                        log(EntitySystemManager::Warning, "Invalid EventHandler: ", className, "::", name, ", return type not void");
                        continue;
                    }

                    int typeId;
                    asDWORD flags;
                    func->GetParam(0, &typeId, &flags);
                    auto* typeInfo = engine->GetTypeInfoById(typeId);
                    
                    if ((typeInfo->GetFlags() & asOBJ_SCRIPT_OBJECT) == 0 || flags != (asTM_INREF | asTM_CONST))
                    {
                        log(EntitySystemManager::Warning, "Invalid EventHandler: ", className, "::", name, ", first parameter must be a const & to a script object ", flags);
                        continue;
                    }
                    auto seqtid = typeId & asTYPEID_MASK_SEQNBR;
                    func->AddRef();
                    cls->eventHandlers.push_back({ seqtid, func });
                }

                if (IsPresentInList(metadata, "InitHandler"))
                {
                    if (func->GetParamCount() != 0)
                    {
                        log(EntitySystemManager::Warning, "Invalid InitHandler: ", className, "::", name, ", illegal parameter count");
                        continue;
                    }

                    if (func->GetReturnTypeId() != 0)
                    {
                        log(EntitySystemManager::Warning, "Invalid InitHandler: ", className, "::", name, ", return type not void");
                        continue;
                    }

                    
                    func->AddRef();
                    cls->eventHandlers.push_back({ EntityEventInitId , func });
                }

                if (IsPresentInList(metadata, "DeinitHandler"))
                {
                    if (func->GetParamCount() != 0)
                    {
                        log(EntitySystemManager::Warning, "Invalid DeinitHandler: ", className, "::", name, ", illegal parameter count");
                        continue;
                    }

                    if (func->GetReturnTypeId() != 0)
                    {
                        log(EntitySystemManager::Warning, "Invalid DeinitHandler: ", className, "::", name, ", return type not void");
                        continue;
                    }


                    func->AddRef();
                    cls->eventHandlers.push_back({ EntityEventDeinitId , func });
                }
            }

            //Iterate properties
            auto pcnt = ti->GetPropertyCount();
            for (unsigned int i = 0; i < pcnt; i++)
            {
                const char* name;
                bool isReference;
                int offset, typeId;
                int r = ti->GetProperty(i, &name, &typeId, nullptr, nullptr, &offset, &isReference);
                assert(r >= 0);
                if (r < 0)
                    continue;
                
                if (isReference)
                    continue;
                
                if (cls->entityReference.has == false && strcmp(name, "entity") == 0)
                {
                    if (typeId == (entityTypeInfo->GetTypeId() | asTYPEID_OBJHANDLE))
                    {
                        cls->entityReference.has = true;
                        cls->entityReference.offset = offset;
                        log(EntitySystemManager::Info, "EntityRef: ", className, "::entity");
                        continue;
                    }
                }

                const char* mtd = builder->GetMetadataStringForTypeProperty(ctid, i);
                if ((mtd != nullptr) && strcmp(mtd, "ComponentRef") == 0)
                {
                    if ((typeId & asTYPEID_OBJHANDLE) == 0)
                    {
                        log(EntitySystemManager::Warning, "Invalid ComponentRef: ", className, "::", name);
                        continue;
                    }
                    auto reflesstid = typeId & asTYPEID_MASK_SEQNBR;
                    auto it = classes.find(reflesstid);
                    if (it == classes.end())
                    {
                        log(EntitySystemManager::Warning, "Invalid ComponentRef: ", className, "::", name);
                        continue;
                    }
                    else
                    {
                        log(EntitySystemManager::Info, "ComponentRef: ", className, "::", name);
                        cls->componentReferences.push_back({reflesstid, ReferenceOffset(true, offset) });
                    }
                }
            }
            ti = ti->GetBaseType();
        }
    }


    system->preallocate();
}

void EntitySystemManager::release()
{
    if (entityTypeInfo)
        entityTypeInfo->Release();
    system = nullptr;
    entityTypeInfo = nullptr;
    classes.clear();
    engine->Release();
}

EntityType * EntitySystemManager::getTypeByMoldId(unsigned int i)
{
    if (i >= entityMolds.size())
        return nullptr;
    return entityMolds[i].get();
}

ComponentClass::ComponentClass(const char * name, asIScriptFunction * constructor, asITypeInfo * typeInfo)
{
    this->name = name;
    this->factory = constructor;
    this->typeInfo = typeInfo;

    factory->AddRef();
    typeInfo->AddRef();
}

ComponentClass::~ComponentClass()
{
    factory->Release();
    typeInfo->Release();
    for (auto& p : eventHandlers)
    {
        p.second->Release();
    }
}

Component::Component(ComponentClass * cls, Entity * owner)
    : componentClass(cls), entity(owner)
{

}

void Component::addRef()
{
    entity->addRef();
}

void Component::release()
{
    entity->release();
}

void Component::releaseObject()
{
    if (object)
        object->Release();
    object = nullptr;
}

Component::~Component()
{
    if (object)
    {
        object->Release();
    }
}

Component * Entity::getComponent(int tid)
{
    for (Component& c : components)
    {
        if (static_cast<int>(c.componentClass->id) == tid)
            return &c;
    }
    return nullptr;
}

bool Entity::getComponentObject(void * ptr, int tid)
{
    if ((tid & asTYPEID_OBJHANDLE) == 0)
    {
        auto* ctx = asGetActiveContext();
        ctx->SetException("Entity::getComponent must be called with a handle to a component type");
        return false;
    }
    
    tid = tid & asTYPEID_MASK_SEQNBR;
    Component* c = getComponent(tid);
    if (c)
    {
        if (c->object)
        {
            c->object->AddRef();
            *static_cast<asIScriptObject**>(ptr) = c->object;
            return true;
        }
    }
    return false;
}

unsigned int Entity::sendSpecialEventNowInContext(int seid, asIScriptContext* ctx)
{
    auto f = type->eventHandlers.find(seid);
    if (f == type->eventHandlers.end())
        return 0;
    else
    {

        unsigned int cnt = 0;
        for (auto& p : f->second)
        {
            auto& c = components[p.componentIndex];

            auto* obj = c.object;
            if (obj == nullptr)
                continue;
            auto* func = c.componentClass->eventHandlers[p.eventIndex].second;
            ctx->Prepare(func);
            ctx->SetObject(obj);
            ctx->Execute();
            ++cnt;
        }
        return cnt;
    }
}

unsigned int Entity::sendEventNowInContext(asIScriptObject * ptr, int tid, asIScriptContext * ctx)
{
    auto p = tid & asTYPEID_MASK_SEQNBR;
    auto f = type->eventHandlers.find(p);
    if (f == type->eventHandlers.end())
        return 0;
    else
    {

        unsigned int cnt = 0;
        for (auto& p : f->second)
        {
            auto& c = components[p.componentIndex];

            auto* obj = c.object;
            if (obj == nullptr)
                continue;
            auto* func = c.componentClass->eventHandlers[p.eventIndex].second;
            ctx->Prepare(func);
            ctx->SetObject(obj);
            ctx->SetArgAddress(0, ptr);
            ctx->Execute();
            ++cnt;
        }
        return cnt;
    }
    return 0;
}

unsigned int Entity::sendEventNow(asIScriptObject * ptr, int tid)
{

    if ((tid & asTYPEID_SCRIPTOBJECT) == 0 || (tid & asTYPEID_OBJHANDLE) != 0)
    {
        auto* ctx = asGetActiveContext();
        if (ctx)
        {
            ctx->SetException("Entity::sendEventNow called with illegal arguments");
            return 0;
        }
    }
    

    auto* ctx = system->engine->RequestContext();
    auto retval = sendEventNowInContext(ptr, tid, ctx);
    system->engine->ReturnContext(ctx);
    return retval;
}

ComponentIterator::ComponentIterator(EntitySystem * sys, VecType * vec)
{
    system = sys;
    if (vec != nullptr && vec->size() > 0 )
    {
        vecIterator = vec->begin();
        vecEnd = vec->end();

        while ((*vecIterator)->dead)
        {
            ++vecIterator;
            if (vecIterator == vecEnd)
            {
                finished = true;
                break;;
            }
        }
    }
    else
        finished = true;
}

asIScriptObject * ComponentIterator::next()
{
    if (finished)
    {
        if (invalidated)
        {
            asIScriptContext* ctx = asGetActiveContext();
            ctx->SetException("ComponentIterator invalidated");
        }
        return nullptr;
    }
    
    asIScriptObject* o = (*vecIterator)->object;
    if (o)
        o->AddRef();

    do 
    {
        ++vecIterator;
        if (vecIterator == vecEnd)
        {
            finished = true;
            break;
        }
    } while ((*vecIterator)->dead);
    return o;
}

void ComponentIterator::release()
{
    system->releaseComponentIterator(this);
}

EntityIterator::EntityIterator(EntitySystem * sys, VecType * vec)
{
    system = sys;
    if (vec != nullptr && vec->size() > 0)
    {
        vecIterator = vec->begin();
        vecEnd = vec->end();

        while ((*vecIterator)->dead)
        {
            ++vecIterator;
            if (vecIterator == vecEnd)
            {
                finished = true;
                break;;
            }
        }
    }
    else
        finished = true;
}

Entity* EntityIterator::next()
{
    if (finished)
    {
        if (invalidated)
        {
            asIScriptContext* ctx = asGetActiveContext();
            ctx->SetException("EntityIterator invalidated");
        }
        return nullptr;
    }

    Entity* o = (*vecIterator);
    
    o->addRef();

    do
    {
        ++vecIterator;
        if (vecIterator == vecEnd)
        {
            finished = true;
            break;
        }
    } while ((*vecIterator)->dead);
    return o;
}

void EntityIterator::release()
{
    system->releaseEntityIterator(this);
}

}

/* Tests for the above
 
-- UNIT TESTS -- AngelScript -- Coroutine

namespace ECSTests
{
    class TestEvent
    {
        int value = 0;
    }
    
    [Component]
    class TestComponent_2
    {
        Entity@ entity;
    }

    [Component]
    class TestComponent
    {
        Entity@ entity;
        bool initCalled = false;
        bool deInitCalled = false;
        int value = 0;

        [InitHandler]
        void init()
        {
            initCalled = true;
        }

        [DeinitHandler]
        void deinit()
        {
            deInitCalled = true;
        }


        [EventHandler]
        void update(const TestEvent&in ev)
        {
            value = ev.value;
        }

    }

    EntityMold@ EM_Test = {
        ComponentInfo<TestComponent>().getId()
    };
    
    
    EntityMold@ EM_Test_2 = {
        ComponentInfo<TestComponent>().getId(),
        ComponentInfo<TestComponent_2>().getId()
    };

    [Test]
    void ComponentTest()
    {
        TestComponent@ tc;
        Entity@ e = ESM::ConstructEntity(EM_Test);

        Assert(e !is null);

        e.getComponent(@tc);

        Assert(tc !is null);

        Assert(tc.entity is e);
        Assert(tc.entity == e);
        Assert(tc.initCalled == false);

        Assert(e.dead == false);
        Assert(e.id > 0);

        ESM::UpdateEntityLists();

        Assert(tc.initCalled == true);

        ESM::KillEntity(e);
        Assert(e.dead == false);
        Assert(e.id > 0);
        Assert(tc.deInitCalled == false);

        ESM::UpdateEntityLists();
        Assert(e.dead == true);
        Assert(e.id == 0);
        Assert(tc.deInitCalled == true);

        Assert(tc.entity.dead == true);

        TestComponent@ tc2;
        e.getComponent(@tc2);

        Assert(tc2 is null);

        Assert(e.dead == true);
        ESM::KillEntity(e);
        Assert(e.dead == true);
        ESM::UpdateEntityLists();
        Assert(e.dead == true);

    }
    
    [Test]
    void EventTest()
    {
        TestComponent@ tc;
        Entity@ e = ESM::ConstructEntity(EM_Test);

        Assert(e !is null);
        e.getComponent(@tc);
        Assert(tc !is null);
        Assert(tc.value == 0);
        
        ESM::UpdateEntityLists();
        
        TestEvent te;
        te.value = 101;
        
        ESM::QueueGlobalEvent(te);
        ESM::SendEvents();
        
        Assert(tc.value == 101);
        
        te.value = 66;
        ESM::QueueLocalEvent(e, te);
        ESM::SendEvents();
        
        Assert(tc.value == 66);
    }

    [Test]
    void IteratorTest()
    {
        //Construct ten entities
        for (uint i = 0; i < 5; i++)
            ESM::ConstructEntity(EM_Test);
        
        for (uint i = 0; i < 5; i++)
            ESM::ConstructEntity(EM_Test_2);
        
        ESM::UpdateEntityLists();
        
        //Send event to all of them
        TestEvent te;
        te.value = 1;
        
        ESM::QueueGlobalEvent(te);
        ESM::SendEvents();
        

        //Iterate through all 
        ComponentIterator<TestComponent> ci;
        
        int tc2s = 0;
        int total = 0;
        TestComponent@ tc;
        
        while ((@tc = ci.next()) !is null)
        {
            Assert(tc.value == 1);
            total += tc.value;
            
            //Count number of entities with TestComponent_2 in them
            
            TestComponent_2@ tc2;
            tc.entity.getComponent(@tc2);
            if (tc2 !is null)
                tc2s += 1;
        }
        
        Assert(total == 10);
        Assert(tc2s == 5);
    }
    
    [Component]
    class ReentrantUpdateEntityListsComponent
    {
        int value = 0;
        [InitHandler]
        void init()
        {
            
            ESM::UpdateEntityLists(); //Throws an exception
            value = 100;
        }

        [DeinitHandler]
        void deinit()
        {
            ESM::UpdateEntityLists(); //Throws an exception
            value = 200;
        }
    }
    
    //UpdateEntityLists is not reentrant and will throw errors if
    //InitHandler or DeinitHandler tries to update the lists
    
    //However the outer UpdateEntityLists catches all exceptions by default
    //and these exceptions never propagate further.
    [Test]
    void ReentrantUpdateEntityListsTest()
    {
        EntityMold@ EM = {
            ComponentInfo<ReentrantUpdateEntityListsComponent>().getId(),
            ComponentInfo<TestComponent>().getId()
        };
        
        
        ReentrantUpdateEntityListsComponent@ c;
        Entity@ e = ESM::ConstructEntity(EM);
        e.getComponent(@c);
        c.value = 0;
        
        ESM::UpdateEntityLists();
        
        Assert(c.value == 0);
        c.value = 1;
        ESM::KillAllEntities();
        ESM::UpdateEntityLists();
        Assert(c.value == 1);
    }
    

    [Component]
    class TestComponentRef
    {
        Entity@ entity;
        [ComponentRef]
        TestComponentRef@ tcr;
        [ComponentRef]
        TestComponent@ tc;
    }

    EntityMold@ EM_TestRef = {
        ComponentInfo<TestComponent>().getId(),
        ComponentInfo<TestComponentRef>().getId()
    };

    [Test]
    void ComponentRefTest()
    {
        TestComponent@ tc;
        TestComponentRef@ tcr;
        Entity@ e = ESM::ConstructEntity(EM_TestRef);

        Assert(e !is null);

        e.getComponent(@tcr);
        e.getComponent(@tc);

        Assert(tcr !is null);

        Assert(tc !is null);


        Assert(tcr.tcr is null); //Component reference to self is always null

        Assert(tcr.tc is tc);
        Assert(tcr.entity is tc.entity);

        ESM::UpdateEntityLists();
        ESM::KillEntity(e);
        ESM::UpdateEntityLists();

        Assert(e.dead == true);
        Assert(e.id == 0);

        Assert(tcr.tcr is null);
        Assert(tcr.tc is tc);

    }

}

-- UNIT TESTS END

*/


