#pragma once
#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <scriptbuilder/scriptbuilder.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>
#include <memory>



/*

    Pseudo-entity-component system for AngelScript
    
    

*/

namespace ASECS
{

template <typename T>
class GenericIterator
{
public:
    std::vector<T*> entities;
    size_t offset;
    T* next()
    {

        T* e = nullptr;
        if (offset < entities.size())
            e = entities[offset];

        ++offset;
        return e;
    }
};

class ComponentClass;
class Entity;
class EntitySystemManager;
class EntitySystem;




struct EntityType
{
    struct EntityComponentReference
    {
        //Reference is stored in
        size_t componentIndex;
        //at offset
        size_t referenceOffset;

        //Reference TO
        size_t toComponent;
    };

    struct ComponentEventHandlerIndex
    {
        size_t componentIndex;
        size_t eventIndex;
    };

    uint32_t hash;
    bool hasCollisions = false;
    std::vector<ComponentClass*> componentTypes;

    //stores all the valid component references
    std::vector<EntityComponentReference> componentReferences;

    //stores all event handlers
    std::unordered_map<unsigned int, std::vector<ComponentEventHandlerIndex>> eventHandlers;
};

struct ReferenceOffset
{
    bool has = false;
    int offset = 0;
    ReferenceOffset()
    {};
    ReferenceOffset(bool h, int offs) : has(h), offset(offs)
    {};
};

class ComponentClass
{
    const char* name;
    unsigned int id;

    asITypeInfo* typeInfo;

    asIScriptFunction* factory;
    std::vector<std::pair<unsigned int, asIScriptFunction*>> eventHandlers;

    ReferenceOffset entityReference;
    std::vector<std::pair<unsigned int, ReferenceOffset>> componentReferences;

public:
    ComponentClass(const char* name, asIScriptFunction* constructor, asITypeInfo*);
    ~ComponentClass();

    friend class Entity;
    friend class EntitySystem;
    friend class EntitySystemManager;
};


class Component
{
    Entity* entity;
    ComponentClass* componentClass;

    asIScriptObject* object = nullptr;

    //see Entity::dead
    bool dead = false;
public:

    Component(Component&& c)
    {
        entity = c.entity;
        componentClass = c.componentClass;
        object = c.object;

        c.entity = nullptr;
        c.componentClass = nullptr;
        c.object = nullptr;
    }

    Component(const Component& c) = delete;

    Component(ComponentClass* cls, Entity* owner);
    ~Component();

    void addRef();

    void release();

    void releaseObject();


    friend class EntitySystem;
    friend class Entity;
    friend class ComponentIterator;
};

class Entity
{
    std::vector<Component> components;

    //Dead entity essentially marks an "removed entity"
    //dead entities will be removed upon cleanUp
    //dead entities are also open for reusing
    //entities marked as dead are ignored in events/component iteration
    bool dead = false;

    //reused entities have all the components already in the lists
    bool reused = false;


    int refCount = 1;
    void setDead(bool new_dead)
    {
        if (new_dead)
            id = 0;
        dead = new_dead;
        for (auto& e : components)
        {
            if (new_dead)
                e.releaseObject();
            e.dead = new_dead;
        }
    }
    const EntityType* type;
    EntitySystem* system;
    unsigned int id;
public:

    bool equals(Entity* e) const
    {
        if (e == nullptr)
            return false;
        if (dead)
            return false;
        if (e->id != id)
            return false;
        return true;
    }

    bool isDead() const
    {
        return dead;
    }

    unsigned int getId() const
    {
        return id;
    }

    void addRef()
    {
        asAtomicInc(refCount);
    }

    void release()
    {
        asAtomicDec(refCount);
        if (refCount <= 0)
        {
            setDead(true);;
            delete this;
        }
    }

    void makeDead()
    {
        setDead(true);
    }

    int getRefCount() const
    {
        return refCount;
    }



    Component* getComponent(int tid);
    bool getComponentObject(void* ptr, int tid);

    unsigned int sendSpecialEventNowInContext(int seid, asIScriptContext* context);
    unsigned int sendEventNowInContext(asIScriptObject* ptr, int tid, asIScriptContext* context);
    unsigned int sendEventNow(asIScriptObject* ptr, int tid);

    friend class EntitySystem;
    friend class Component;
    friend class EntityIterator;
    friend class EntitySystemManager;
};


class ECSIterator
{
public:
    bool finished = false;
    bool invalidated = false;
};

class ComponentIterator : public ECSIterator
{
    typedef std::vector<Component*> VecType;

    VecType::iterator vecIterator;
    VecType::iterator vecEnd;
    EntitySystem* system;
public:
    ComponentIterator(EntitySystem* sys, VecType* vec);

    asIScriptObject* next();
    void release();
};


class EntityIterator : public ECSIterator
{
    typedef std::vector<Entity*> VecType;

    VecType::iterator vecIterator;
    VecType::iterator vecEnd;
    EntitySystem* system;
public:
    EntityIterator(EntitySystem* sys, VecType* vec);

    Entity* next();
    void release();
};


/*! \brief A single self contained entity system

    Many of the methods here are designed to be directly called by AngelScript
    interface and may therefore set AngelScript exceptions on failure.
*/
class EntitySystem
{
    struct EntityEvent
    {
        unsigned int id;
        asIScriptObject* event;
    };

    std::vector<EntityEvent> preparedGlobalEvents;
    std::vector<std::pair<Entity*, EntityEvent>> preparedLocalEvents;

    //used for "double buffering"
    std::vector<EntityEvent> preparedGlobalEventsSwap;
    std::vector<std::pair<Entity*, EntityEvent>> preparedLocalEventsSwap;

    std::unordered_map<unsigned int, std::vector<Component*>> componentsByClass;
    std::unordered_map<unsigned int, std::vector<std::pair<Component*, unsigned int>>> componentsByEvent;

    EntitySystemManager* manager;
    std::vector<Entity*> allEntities;

    std::vector<Entity*> entitiesToKill;
    std::vector<Entity*> entitiesToSpawn;

    //used for "double buffering"
    std::vector<Entity*> entitiesToKillSwap;
    std::vector<Entity*> entitiesToSpawnSwap;

    std::set<ComponentIterator*> activeComponentIterators;
    std::set<EntityIterator*> activeEntityIterators;

    std::unordered_map<uint32_t, std::unique_ptr<std::vector<Entity*>>> deadEntitiesByTypeHash;

    void buildEntityComponents(Entity* entity);
    void buildEntityComponentReferences(Entity* entity, const EntityType* type);
    

    asIScriptEngine* engine;

    void invalidateIterators();
    void clearPreparedEvents();

    size_t stat_entityIteratorsConstructed = 0;
    size_t stat_componentIteratorsConstructed = 0;
    size_t stat_entityConstructions = 0;
    size_t stat_globalEventsSent = 0;
    size_t stat_localEventsSent = 0;
    unsigned int lastEntityId = 0;
    unsigned int getNextEntityId()
    {
        ++lastEntityId;
        return lastEntityId;
    }
public:
    EntitySystem(EntitySystemManager*,asIScriptEngine*);
    ~EntitySystem();
    
    void cleanUp();
    
    //! Releases all data. Use preallocate() to restore full functionality.
    void clear();

    /*! \brief Send all prepared events
    
        Recursive call to this function will result in AngelScript context
        exception.
    */
    bool sendEvents();
    
    /*! \brief Update entity state lists
    
        Entities marked to be killed will be removed and new entities
        contructed will be added. This will call the init and deinit handlers.
        
        Recursive call to this function will result in AngelScript context
        exception.
    */
    
    void updateEntityLists();
    void prepareGlobalEvent(asIScriptObject*, int);
    void prepareLocalEvent(Entity*, asIScriptObject*, int);

    Entity* constructEntity(const EntityType* type);
    Entity* constructEntity(unsigned int moldId);
    void killEntity(Entity* e);
    void killAllEntities();

    ComponentIterator* constructComponentIterator(asITypeInfo* type);
    void releaseComponentIterator(ComponentIterator* cls);


    EntityIterator* constructEntityIterator();
    void releaseEntityIterator(EntityIterator*);

    void logDebugInfo();

    void preallocate();
    friend class Entity;


};

//! Global entity system information
class EntitySystemManager
{
    std::unordered_map<uint32_t, size_t> moldIdsByHash;
    std::vector<std::unique_ptr<EntityType>> entityMolds;

    std::unordered_map<unsigned int, std::unique_ptr<ComponentClass>> classes;
    asIScriptEngine* engine;
    asITypeInfo* entityTypeInfo = nullptr;

    template <typename T, typename ... Args >
    void ilog(std::stringstream & logBuffer, T t, Args ... b)
    {
        logBuffer << t;
        ilog(logBuffer, b...);
    }
    template <typename T>
    void ilog(std::stringstream & logBuffer, T t)
    {
        logBuffer << t;
    }


    std::unique_ptr<EntitySystem> system;

    void* logCallbackUserPtr = nullptr;
    void (*logCallback)(void*, const char*, int) = nullptr;
public:

    enum LogLevel
    {
        Info = 0,
        Warning,
        Error
    };

    EntityType* entityMoldFactory(uint32_t*);

    template <typename ... Args >
    void log(LogLevel ll, Args ... b)
    {
        if (logCallback)
        {
            std::stringstream logBuffer;
            ilog(logBuffer, b...);
            logCallback(logCallbackUserPtr, logBuffer.str().c_str(), (int) ll);
        }
    }

    void setLogCallback(void (*callback)(void*, const char*, int), void* userPtr)
    {
        logCallbackUserPtr = userPtr;
        logCallback = callback;
    }

    int getMoldId(CScriptArray*);
    int getMoldId(const std::vector<uint32_t>&);
    
    //! Register AngelScript interface
    void registerEngine(asIScriptEngine* engine);
    
    /*! \brief Gather entity meta information from AngelScript module
     
        This must be called on all modules that are going to be used
        with the entity system before global variable initialization.
    */
    void initEntityClasses(CScriptBuilder* builder);
    
    void release();
    EntityType* getTypeByMoldId(unsigned int);

    EntitySystem* getSystem();
    friend class EntitySystem;
    
};


}
