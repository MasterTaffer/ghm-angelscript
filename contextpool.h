#pragma once
#include <angelscript.h>
#include <vector>
#include <mutex>

//! The simplest implementation of a context pool for AngleScript
class ASContextPool
{
    std::mutex mutex;
    asIScriptEngine* engine = nullptr;
    std::vector<asIScriptContext*> pool;
    void lockless_updatePool();
    void lockless_disconnect();
    bool hasExceptionCb = false;
    asSFuncPtr exceptionCb;
    void* exceptionCbObject;
    int exceptionCbCallConv;
public:
    //! Connect to an engine
    void connect(asIScriptEngine* ase);
    //! Disconnect from the engine
    void disconnect();
    //! Pre-allocate contexts
    void updatePool();
    //! Set exception callback for all contexts
    void setExceptionCallback(asSFuncPtr, void*, int);
    //! Clear exception callback
    void clearExceptionCallback();
    //! Return context to the pool
    void returnContext(asIScriptEngine* ase, asIScriptContext* ctx);
    //! Request context from the pool
    asIScriptContext* requestContext(asIScriptEngine* ase);
};
