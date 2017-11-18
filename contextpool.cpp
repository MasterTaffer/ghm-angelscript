#include <exception>
#include <cassert>
#include "contextpool.h"


static asIScriptContext* s_requestContext(asIScriptEngine* ase, void* v)
{
    ASContextPool* cp = static_cast<ASContextPool*>(v);
    if (cp == nullptr)
        return ase->CreateContext();
    return cp->requestContext(ase);
}

static void s_returnContext(asIScriptEngine* ase, asIScriptContext* ctx, void* v)
{
    ASContextPool* cp = static_cast<ASContextPool*>(v);
    if (cp == nullptr)
    {
        ctx->Release();
        return;
    }
    cp->returnContext(ase, ctx);
}

void ASContextPool::lockless_updatePool()
{
    const int minimum = 1;
    const int allocTo = 6;

    if (engine == nullptr)
        throw new std::logic_error("ASContextPool not connected");

    if (pool.size() <= minimum)
    {
        for (int i = pool.size(); i < allocTo; i++)
        {
            pool.push_back(engine->CreateContext());
        }
    }
    assert(pool.size() != 0);
}

void ASContextPool::lockless_disconnect()
{
    for (auto* ctx : pool)
        ctx->Release();
    pool.clear();
    if (engine)
    {
        engine->SetContextCallbacks(nullptr, nullptr, nullptr);
        engine->Release();
    }

    engine = nullptr;
    hasExceptionCb = false;
}



void ASContextPool::returnContext(asIScriptEngine * ase, asIScriptContext * ctx)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (ase != engine)
    {
        ctx->Release();
        return;
    }
    ctx->Unprepare();
    ctx->ClearExceptionCallback();
    pool.push_back(ctx);
}

asIScriptContext * ASContextPool::requestContext(asIScriptEngine * ase)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (ase != engine)
    {
        return ase->CreateContext();
    }

    if (pool.size() == 0)
    {
        lockless_updatePool();
    }

    asIScriptContext* p = pool.back();
    pool.pop_back();
    
    if (hasExceptionCb)
        p->SetExceptionCallback(exceptionCb, exceptionCbObject, exceptionCbCallConv);
    return p;
}

void ASContextPool::connect(asIScriptEngine * ase)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (ase == nullptr)
        throw new std::logic_error("ASContextPool connect called with nullptr");
    if (engine)
        lockless_disconnect();
    ase->AddRef();
    ase->SetContextCallbacks(s_requestContext, s_returnContext, this);
    engine = ase;

}

void ASContextPool::disconnect()
{
    std::lock_guard<std::mutex> lock(mutex);
    lockless_disconnect();
}


void ASContextPool::updatePool()
{
    std::lock_guard<std::mutex> lock(mutex);
    lockless_updatePool();
}

void ASContextPool::setExceptionCallback(asSFuncPtr func, void * obj, int callconv)
{
    hasExceptionCb = true;
    exceptionCb = func;
    exceptionCbObject = obj;
    exceptionCbCallConv = callconv;
}

void ASContextPool::clearExceptionCallback()
{
    hasExceptionCb = false;
}
