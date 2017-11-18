#include <cassert>
#include <vector>
#include "thread.h"

static ASThread* getASThread()
{
    asIScriptContext *ctx = asGetActiveContext();
    ASThread* thread = static_cast<ASThread*>(ctx->GetUserData(ASThread_ContextUD));
    if (thread == nullptr)
    {
        ctx->SetException("Cannot call global thread messaging in the main thread");
        return nullptr;
    }
    return thread;
}

static void sendInThread(CScriptAny* any)
{
    auto* thread = getASThread();
    if (thread == nullptr)
        return;
    thread->outgoing.send(any);	
}

static CScriptAny* receiveForeverInThread()
{
    auto* thread = getASThread();
    if (thread == nullptr)
        return nullptr;
    return thread->incoming.receiveForever();
}

static CScriptAny* receiveInThread(uint64_t timeout)
{
    auto* thread = getASThread();
    if (thread == nullptr)
        return nullptr;
    return thread->incoming.receive(timeout);
}



static ASThread* startThread(asIScriptFunction* func)
{
    asIScriptContext *ctx = asGetActiveContext();
    asIScriptEngine *ase = ctx->GetEngine();
    ASThread* thread = new ASThread(ase, func);


    auto* threadType = ase->GetTypeInfoByName("Thread");

    ase->NotifyGarbageCollectorOfNewObject(thread, threadType);
    return thread;
}




void RegisterThread(asIScriptEngine* ase)
{
    int r = 0;

    r = ase->RegisterFuncdef("void ThreadFunction()");
    assert(r >= 0);


    r = ase->RegisterObjectType("Thread", 0, asOBJ_REF | asOBJ_GC);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Thread", asBEHAVE_ADDREF, "void f()", asMETHOD(ASThread, addRef), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Thread", asBEHAVE_RELEASE, "void f()", asMETHOD(ASThread, release), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Thread", asBEHAVE_SETGCFLAG, "void f()", asMETHOD(ASThread, setGCFlag), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Thread", asBEHAVE_GETGCFLAG, "bool f()", asMETHOD(ASThread, getGCFlag), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Thread", asBEHAVE_GETREFCOUNT, "int f()", asMETHOD(ASThread, getRefCount), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Thread", asBEHAVE_ENUMREFS, "void f(int&in)", asMETHOD(ASThread, enumReferences), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Thread", asBEHAVE_RELEASEREFS, "void f(int&in)", asMETHOD(ASThread, releaseAllReferences), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Thread", "bool run()", asMETHOD(ASThread, run), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Thread", "bool isFinished()", asMETHOD(ASThread, isFinished), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Thread", "void suspend()", asMETHOD(ASThread, suspend), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Thread", "void send(any &in)", asMETHOD(ASThread, send), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Thread", "any@ receiveForever()", asMETHOD(ASThread, receiveForever), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Thread", "any@ receive(uint64)", asMETHOD(ASThread, receive), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Thread", "int wait(uint64)", asMETHOD(ASThread, wait), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->SetDefaultNamespace("Thread");
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("any@ Receive(uint64 timeout)", asFUNCTION(receiveInThread), asCALL_CDECL);
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("any@ ReceiveForever()", asFUNCTION(receiveForeverInThread), asCALL_CDECL);
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("void Send(any&)", asFUNCTION(sendInThread), asCALL_CDECL);
    assert(r >= 0);

    r = ase->RegisterGlobalFunction("Thread@ CreateThread(ThreadFunction @func)", asFUNCTION(startThread), asCALL_CDECL);
    assert(r >= 0);

    r = ase->SetDefaultNamespace("");
    assert(r >= 0);
    
#ifdef NDEBUG
    (void)(r);
#endif
}

void ASMailbox::send(CScriptAny * any)
{
    if (any == nullptr)
        return;
    any->AddRef();
    dbg += 1;

    {
        std::lock_guard<std::mutex> lk(mutex);
        box.push_back(any);
    }

    cv.notify_one();
}

CScriptAny * ASMailbox::receiveForever()
{
    CScriptAny* p = nullptr;
    do
    {
        p = receive(10000);
        if (p == nullptr)
        {
            //Waited for 10 seconds...
            //Maybe do something about it?
        }
    } while (p == nullptr);
    return p;
}

CScriptAny * ASMailbox::receive(uint64_t timeout)
{
    std::unique_lock<std::mutex> lk(mutex);
    auto dur = std::chrono::milliseconds(timeout);
    if (cv.wait_for(lk, dur, [&] {return box.size() != 0; }))
    {
        auto* p = box.front();
        box.erase(box.begin());
        return p;
    }
    return nullptr;
}

void ASMailbox::enumReferences(asIScriptEngine * engine)
{
    for (auto m : box)
        if (m != nullptr)
            engine->GCEnumCallback(m);
}

int ASMailbox::boxSize()
{
    return box.size();
}

void ASMailbox::release()
{
    std::lock_guard<std::mutex> lk(mutex);
    for (auto f : box)
        if (f != nullptr)
            f->Release();
    box.clear();
}

void ASThread::static_internal_run(ASThread * t)
{
    t->internal_run();
}


void ASThread::internal_run()
{
    //This is the entry point for the threads

    {
        //Lock the thread state
        std::lock_guard<std::mutex> lk(mutex);

        //In case the reference to the function was lost
        if (!func)
        {
            running = false;
            release();
            return;
        }

        //Request a new context from the engine
        if (context == nullptr)
            context = engine->RequestContext();

        //If that fails...
        if (context == nullptr)
        {
            engine->WriteMessage("",0,0,asMSGTYPE_ERROR,"Failed to start thread: no available context");
            running = false;
            release();
            return;
        }

        //Otherwise... go!
        context->Prepare(func);
        context->SetUserData(this, ASThread_ContextUD);
    }

    //Execute without lock
    context->Execute();

    {
        //Cleanup
        std::lock_guard<std::mutex> lk(mutex);

        running = false;
        context->SetUserData(nullptr, ASThread_ContextUD);
        engine->ReturnContext(context);
        context = nullptr;

        //Notify the finished conditional variable
        finished_cv.notify_all();
    }
    release();
    asThreadCleanup();
}

void ASThread::addRef()
{
    gcFlag = false;
    asAtomicInc(ref);
}

void ASThread::suspend()
{
    std::lock_guard<std::mutex> lk(mutex);
    if (context)
    {
        context->Suspend();
    }
}

void ASThread::release()
{
    gcFlag = false;

    if (asAtomicDec(ref) <= 0)
    {
        releaseAllReferences(nullptr);

        if (thread.joinable())
            thread.join();
        delete this;
    }
}

void ASThread::setGCFlag()
{
    gcFlag = true;
}

bool ASThread::getGCFlag()
{
    return gcFlag;
}

int ASThread::getRefCount()
{
    return ref;
}

void ASThread::enumReferences(asIScriptEngine *)
{
    incoming.enumReferences(engine);
    outgoing.enumReferences(engine);
    engine->GCEnumCallback(engine);
    if (context != nullptr)
        engine->GCEnumCallback(context);
    if (func != nullptr)
        engine->GCEnumCallback(func);
}

int ASThread::wait(uint64_t timeout)
{
    {
        std::lock_guard<std::mutex> lk(mutex);
        if (!thread.joinable())
            return -1;
    }
    {
        std::unique_lock<std::mutex> lk(mutex);
        auto dur = std::chrono::milliseconds(timeout);
        if (finished_cv.wait_for(lk, dur, [&] {return !running; }))
        {
            thread.join();
            return 1;
        }
    }
    return 0;
}

void ASThread::send(CScriptAny * any)
{
    incoming.send(any);
}

CScriptAny * ASThread::receiveForever()
{
    return outgoing.receiveForever();
}

CScriptAny * ASThread::receive(uint64_t timeout)
{
    return outgoing.receive(timeout);
}

bool ASThread::isFinished()
{
    std::lock_guard<std::mutex> lk(mutex);
    if (running)
        return false;
    return true;
}

bool ASThread::run()
{
    std::lock_guard<std::mutex> lk(mutex);
    if (!func)
        return false;
    if (running)
        return false;
    running = true;

    addRef();
    thread = std::thread(static_internal_run, this);
    return true;
}

ASThread::ASThread(asIScriptEngine * engine, asIScriptFunction * func) : engine(engine), func(func)
{
}

void ASThread::releaseAllReferences(asIScriptEngine *)
{
    outgoing.release();
    incoming.release();
    
    std::lock_guard<std::mutex> lk(mutex);
    if (func)
        func->Release();
    if (context)
        engine->ReturnContext(context);

    engine = nullptr;
    context = nullptr;
    func = nullptr;
}



/* Tests for the above

    Indeterministic threading 'unit tests'... take these with a grain of salt.

-- UNIT TESTS -- AngelScript -- Thread

        
    void receiveAndSend()
    {
        for (uint i = 0; i < 1000; i++)
        {
            any@ object = Thread::ReceiveForever();
            int num = 0;
            object.retrieve(num);
            Thread::Send(any(num + 1));
        }
    }

    [Test]
    void TestThreadMessaging()
    {
        Thread@ thread = Thread::CreateThread(@receiveAndSend);
        Assert(thread.isFinished() == true); //isFinished returns true if the thread is not running
        
        Assert(thread.run());
        Assert(thread.isFinished() == false);
        for (uint i = 0; i < 1000; i++)
        {
            thread.send(any(560));
            any@ object = thread.receiveForever();

            int num = 0;
            Assert(object.retrieve(num));
            Assert(num == 561);
        }

        //10 second better be enough.
        Assert (thread.wait(10000) != 0);

        Assert(thread.isFinished() == true);
    }


    void circleSend()
    {

        while (true)
        {
            any@ object = Thread::Receive(1);
            if (object is null)
                continue;

            Thread@ next;
            object.retrieve(@next);

            int num = 0;
            @object = Thread::ReceiveForever();
            object.retrieve(num);

            next.send(any(num + 1));
            break;
        }
    }

    void circleLast()
    {
        any@ object = Thread::ReceiveForever();
        int num = 0;
        object.retrieve(num);
        Thread::Send(any(num));
    }

    [Test]
    void TestInterThreadMessaging()
    {
        Thread@[] threads;

        for (uint i = 0; i < 5; i++)
        {
            Thread@ t = Thread::CreateThread(@circleSend);
            threads.insertLast(@t);

            t.run();
        }

        Thread@ last = Thread::CreateThread(@circleLast);
        threads.insertLast(@last);

        last.run();

        for (uint i = 0; i < 5; i++)
        {
            Thread@ t = threads[i];
            Thread@ next = threads[i + 1];
            t.send(any(@next));
        }
        threads[0].send(any(654));

        //Wait for 10 secs max
        Assert(last.wait(10000) != 0);


        for (uint i = 0; i < threads.length(); i++)
        {
            Thread@ t = threads[i];
            Assert(t.wait(10000) != 0);
        }
        any@ object = last.receiveForever();
        int val = 0;
        object.retrieve(val);
        Assert(val == 654 + 5);
    }



-- UNIT TESTS END

*/


