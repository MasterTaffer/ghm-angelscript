#pragma once
#include <angelscript.h>
#include <scriptany/scriptany.h>
#include <vector>
#include <cassert>
#include <set>
#include <iostream>

class ASCoroutineStack;
class ASCoroutine
{
    asIScriptEngine* engine;
public:
    //! Increments reference counter
    void addRef();

    //! Decrements reference counter
    void release();

    //! Returns true if the Coroutine has finished
    bool isFinished();
    
    //! Send item to the Coroutine mailbox
    void send(CScriptAny* any);
private:

    //These functions are meant to be only called from AngelScript context
    
    void run();

    ASCoroutine(asIScriptContext* ctx, ASCoroutineStack* stack);


    asIScriptContext* context = nullptr;
    ASCoroutineStack* stack = nullptr;
    bool finished = false;

    std::vector<CScriptAny*> mailbox;
    
    //GC stuff
    void setGCFlag();
    bool getGCFlag();
    int getRefCount();
    void enumReferences(asIScriptEngine*);
    void releaseAllReferences(asIScriptEngine*);
    
    bool gcFlag = false;
    int ref = 1;

    friend class ASCoroutineStack;
};

//! Takes care of coroutines
class ASCoroutineStack
{
    asITypeInfo* coroutineType = nullptr;
    std::set<ASCoroutine*> allCoroutines;
    asIScriptContext* currentContext = nullptr;
    std::vector<ASCoroutine*> stack;
    friend class ASCoroutine;
    
    //These functions are designed to be called from AngelScript context only
    //and set AngelScript exceptions on failure
    void yield();
    CScriptAny* receive();
    unsigned int getMailboxSize();
    ASCoroutine* startCoroutine(asIScriptFunction *func);
public:

    ~ASCoroutineStack();
    
    /*! \brief Release the references held to AngelScript objects
        
        All coroutines will be in invalid state after this. This should
        be only called before destroying the script engine.
    */
    void releaseResources();

    /*! \brief Run script function with coroutines enabled
    
        If this is not used coroutines will not work as expected. Should be
        used as an replacement for context->Execute().
        
        \p context the context to use in prepared state
    */
    int runMainThread(asIScriptContext* context);

    //! Registers the AngelScript interface
    void registerInterface(asIScriptEngine* ase);
};

/*! \brief Register Coroutine interface to the engine

    If the ASCoroutineStack passed in is deleted after this call, all 
    coroutine related registered functions will be invalid and will likely
    result in program crash.
    
    Do use only this or ASCoroutineStack::registerInterface, not both.
    This is merely a wrapper call to crstack->registerInterface.
*/    
void RegisterCoroutine(asIScriptEngine* ase, ASCoroutineStack* crstack);


