#include "coroutine.h"

void RegisterCoroutine(asIScriptEngine* ase, ASCoroutineStack* crstack)
{
    crstack->registerInterface(ase);

}

void ASCoroutineStack::registerInterface(asIScriptEngine* ase)
{
    int r = 0;

    r = ase->RegisterFuncdef("void CoroutineFunction()");
    assert(r >= 0);


    r = ase->RegisterObjectType("Coroutine", 0, asOBJ_REF | asOBJ_GC);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Coroutine", asBEHAVE_ADDREF, "void f()", asMETHOD(ASCoroutine, addRef), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Coroutine", asBEHAVE_RELEASE, "void f()", asMETHOD(ASCoroutine, release), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("Coroutine", asBEHAVE_SETGCFLAG, "void f()", asMETHOD(ASCoroutine, setGCFlag), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Coroutine", asBEHAVE_GETGCFLAG, "bool f()", asMETHOD(ASCoroutine, getGCFlag), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Coroutine", asBEHAVE_GETREFCOUNT, "int f()", asMETHOD(ASCoroutine, getRefCount), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Coroutine", asBEHAVE_ENUMREFS, "void f(int&in)", asMETHOD(ASCoroutine, enumReferences), asCALL_THISCALL);
    assert(r >= 0);
    r = ase->RegisterObjectBehaviour("Coroutine", asBEHAVE_RELEASEREFS, "void f(int&in)", asMETHOD(ASCoroutine, releaseAllReferences), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Coroutine", "void run()", asMETHOD(ASCoroutine, run), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Coroutine", "bool isFinished()", asMETHOD(ASCoroutine, isFinished), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("Coroutine", "void send(any &in)", asMETHOD(ASCoroutine, send), asCALL_THISCALL);
    assert(r >= 0);
    
    r = ase->SetDefaultNamespace("Coroutine");
    assert(r >= 0);

    r = ase->RegisterGlobalFunction(
        "Coroutine@ CreateCoroutine(CoroutineFunction @func)",
        asMETHOD(ASCoroutineStack, startCoroutine),
        asCALL_THISCALL_ASGLOBAL,
        this);
    
    assert(r >= 0);

    r = ase->RegisterGlobalFunction(
        "void Yield()",
        asMETHOD(ASCoroutineStack, yield),
        asCALL_THISCALL_ASGLOBAL,
        this);
    
    assert(r >= 0);

    r = ase->RegisterGlobalFunction(
        "any@ Receive()",
        asMETHOD(ASCoroutineStack, receive),
        asCALL_THISCALL_ASGLOBAL,
        this);
    
    assert(r >= 0);

    r = ase->RegisterGlobalFunction(
        "uint GetMailboxSize()",
        asMETHOD(ASCoroutineStack, getMailboxSize),
        asCALL_THISCALL_ASGLOBAL,
        this);
    
    assert(r >= 0);

    r = ase->SetDefaultNamespace("");
    assert(r >= 0);
    
#ifdef NDEBUG
    (void)(r);
#endif
}

void ASCoroutine::addRef()
{
    asAtomicInc(ref);
    gcFlag = false;
}

void ASCoroutine::release()
{
    gcFlag = false;

    if (asAtomicDec(ref) <= 0)
    {
        releaseAllReferences(nullptr);
        delete this;
    }
}

void ASCoroutine::setGCFlag()
{
    gcFlag = true;
}

bool ASCoroutine::getGCFlag()
{
    return gcFlag;
}

int ASCoroutine::getRefCount()
{
    return ref;
}

void ASCoroutine::enumReferences(asIScriptEngine*)
{

    for (auto m : mailbox)
        if (m != nullptr)
            engine->GCEnumCallback(m);
    if (context != nullptr)
        engine->GCEnumCallback(context);
}

void ASCoroutine::releaseAllReferences(asIScriptEngine*)
{
    for (auto& m : mailbox)
    {
        if (m != nullptr)
        {
            m->Release();
            m = nullptr;
        }
    }

    if (context)
    {
        engine->ReturnContext(context);
        context = nullptr;
    }
}

ASCoroutine::ASCoroutine(asIScriptContext* ctx, ASCoroutineStack* stack) : context(ctx), stack(stack)
{
    engine = ctx->GetEngine();
}

bool ASCoroutine::isFinished()
{
    return (finished || context == nullptr);
}


void ASCoroutine::send(CScriptAny* any)
{
    if (any != nullptr)
        any->AddRef();

    mailbox.push_back(any);
}


void ASCoroutine::run()
{

    auto ctx = asGetActiveContext();
    if (finished)
    {
        ctx->SetException("Coroutine already finished");
        return;
    }
    if (context == nullptr)
    {
        ctx->SetException("Coroutine context already released");
        return;
    }
    stack->stack.push_back(this);
    addRef();
    ctx->Suspend();
}

int ASCoroutineStack::runMainThread(asIScriptContext* context)
{
    while (true)
    {
        currentContext = context;
        int r = context->Execute();
        currentContext = nullptr;

        if (r == asEXECUTION_FINISHED)
            return r;
        if (stack.size() == 0)
            return r;

        while (stack.size() > 0)
        {
            //assert(r == asEXECUTION_SUSPENDED);

            size_t prevSize = stack.size();
            auto* cr = *stack.rbegin();
            if (cr->finished)
            {
                cr->release();
                stack.resize(stack.size() - 1);
                continue;
            }

            currentContext = cr->context;
            int r2 = cr->context->Execute();
            currentContext = nullptr;

            if (r2 == asEXECUTION_FINISHED)
            {
                cr->finished = true;
                cr->releaseAllReferences(nullptr);
                if (allCoroutines.erase(cr) >= 1)
                    cr->release();
            }

            if (r2 == asEXECUTION_EXCEPTION)
            {
                int ln, col;
                const char* sec;
                ln = cr->context->GetExceptionLineNumber(&col, &sec);
                std::string message = std::string("Coroutine exception: ")+cr->context->GetExceptionString();
                cr->context->GetEngine()->WriteMessage(sec, ln, col, asMSGTYPE_WARNING, message.c_str());

                for (auto c: stack)
                {
                    c->finished = true;
                    c->releaseAllReferences(nullptr);
                    c->release();
                    if (allCoroutines.erase(c) >= 1)
                        c->release();
                }
                stack.resize(0);
                context->Abort();
                return asEXECUTION_EXCEPTION;
            }

            size_t newSize = stack.size();
            if (newSize > prevSize) //New coroutine run called
            {
                assert(r2 == asEXECUTION_SUSPENDED);

            }
            else
            {
                cr->release();
                stack.resize(stack.size() - 1);
            }
        }
    }
}



ASCoroutineStack::~ASCoroutineStack()
{
    releaseResources();
}

void ASCoroutineStack::releaseResources()
{
    if (coroutineType)
    {
        coroutineType->Release();
        coroutineType = nullptr;
    }

    for (auto* p : allCoroutines)
    {
        p->releaseAllReferences(nullptr);
        p->release();
    }
    allCoroutines.clear();
}

ASCoroutine* ASCoroutineStack::startCoroutine(asIScriptFunction *func)
{
    auto ctx = asGetActiveContext();
    auto ase = ctx->GetEngine();
    if (func == nullptr)
    {
        ctx->SetException("Null passed to StartCoroutine");
        return nullptr;
    }
    ASCoroutine* coroutine = new ASCoroutine(ase->RequestContext(), this);
    if (coroutineType == nullptr)
    {
        coroutineType = ase->GetTypeInfoByName("Coroutine");
        coroutineType->AddRef();
    }
    coroutine->addRef();
    allCoroutines.insert(coroutine);
    

    ase->NotifyGarbageCollectorOfNewObject(coroutine, coroutineType);

    coroutine->context->Prepare(func);
    func->Release();
    return coroutine;
}

void ASCoroutineStack::yield()
{
    auto ctx = asGetActiveContext();
    if (stack.size() == 0)
    {
        ctx->SetException("Cannot yield the main thread");
        return;
    }
    ctx->Suspend();
}

CScriptAny* ASCoroutineStack::receive()
{
    if (stack.size() == 0)
    {
        auto ctx = asGetActiveContext();
        ctx->SetException("Cannot receive in the main thread");
        return nullptr;
    }
    auto* cr = *stack.rbegin();
    if (cr->mailbox.size() == 0)
    {
        auto ctx = asGetActiveContext();
        ctx->SetException("Coroutine mailbox empty");
        return nullptr;

    }
    CScriptAny* csa = cr->mailbox[0];
    cr->mailbox.erase(cr->mailbox.begin());
    return csa;
}

unsigned int ASCoroutineStack::getMailboxSize()
{
    if (stack.size() == 0)
    {
        return 0;
    }
    auto* cr = *stack.rbegin();
    return cr->mailbox.size();
}





/* Tests for the above

    Coroutine tests

-- UNIT TESTS -- AngelScript -- Coroutine

void emptyCoroutineFunction()
{

}

[Test]
void TestCoroutineCreation()
{
    Assert(Coroutine::CreateCoroutine(@emptyCoroutineFunction) !is null);

    CoroutineFunction@ cf = emptyCoroutineFunction;

    Coroutine@ crt = Coroutine::CreateCoroutine(@cf);
    Assert(crt !is null);

    Assert(crt.isFinished() == false);

    crt.run();

    Assert(crt.isFinished());	

}

class SimpleClass
{
    int value = 0;
    void setValueToOne()
    {
        value = 1;
    }
}

[Test]
void TestCoroutineObject()
{
    SimpleClass sc;

    CoroutineFunction@ cf = CoroutineFunction(sc.setValueToOne);

    Coroutine@ crt = Coroutine::CreateCoroutine(@cf);
    Assert(crt !is null);

    Assert(crt.isFinished() == false);

    Assert(sc.value == 0);
    crt.run();

    Assert(crt.isFinished());
    Assert(sc.value == 1);
}


void yieldOnceCoroutineFunction()
{
    Coroutine::Yield();
}

[Test]
void TestCoroutineYield()
{
    Coroutine@ crt = Coroutine::CreateCoroutine(@yieldOnceCoroutineFunction);

    Assert(crt.isFinished() == false);
    crt.run();

    Assert(crt.isFinished() == false);
    crt.run();

    Assert(crt.isFinished());
}


void shouldReceiveCoroutineFunction()
{
    Assert(Coroutine::GetMailboxSize() == 1);
    any@ object = Coroutine::Receive();

    SimpleClass sc;
    
    Assert(object.retrieve(sc));
    Assert(sc.value == 1);
}



[Test]
void TestCoroutineMessage()
{
    Coroutine@ crt = Coroutine::CreateCoroutine(@shouldReceiveCoroutineFunction);

    SimpleClass sc;
    sc.value = 1;

    Assert(crt.isFinished() == false);
    crt.send(any(sc));
    crt.run();

    Assert(crt.isFinished());
}


void nestedCoroutineRunner()
{
    auto crt = Coroutine::CreateCoroutine(@yieldOnceCoroutineFunction);
    Assert(crt.isFinished() == false);
    crt.run();
    Assert(crt.isFinished() == false);

    yieldOnceCoroutineFunction();

    crt.run();
    Assert(crt.isFinished() == true);

    @crt = Coroutine::CreateCoroutine(@yieldOnceCoroutineFunction);
    Assert(crt.isFinished() == false);

    yieldOnceCoroutineFunction();

    crt.run();
    Assert(crt.isFinished() == false);
    crt.run();
    Assert(crt.isFinished() == true);
}

[Test]
void TestNestedCoroutine()
{
    Coroutine@ crt = Coroutine::CreateCoroutine(@nestedCoroutineRunner);
    Assert(crt.isFinished() == false);
    crt.run();
    Assert(crt.isFinished() == false);
    crt.run();
    Assert(crt.isFinished() == false);
    crt.run();
    Assert(crt.isFinished() == true);
}


//
//  The following tests mess with sending coroutines to each other,
//  possibly recursively calling each other
//
//  One must remember while investigating the control flow below, that the
//  way a coroutine is suspended via Yield is exactly the same as calling
//  run() for another coroutine.
//
//  The active coroutines are also in a "stack": the coroutine stack. When
//  someone calls coroutine.run(), the coroutine is pushed to the stack,
//  regardless if it already exists somewhere in the stack. The running
//  coroutine is always the topmost on the stack. When the running coroutine
//  yields or returns, it is popped from the stack and the next routine on 
//  the stack is continued.



void passingCoroutinesFunc()
{

    Coroutine@ myroutine;
    
    any@ object = Coroutine::Receive();
    Assert(object.retrieve(@myroutine));

    myroutine.run();
    Coroutine::Yield();
}

[Test]
void TestPassingCoroutinesSimple()
{
    Coroutine@ crt = Coroutine::CreateCoroutine(@passingCoroutinesFunc);

    Assert(crt.isFinished() == false);
    crt.send(any(@crt));
    crt.run();

    Assert(crt.isFinished());
}





//  Rather complex test with a quite convoluted control flow
//
//  CoroutineMonitor is used to observe the control flow in the coroutines

class CoroutineMonitor
{
    int[] values;
}

void passingCoroutinesFuncComplex()
{
    CoroutineMonitor@ monitor;
    Coroutine@ myroutine;
    
    //Receive the monitor object
    any@ object = Coroutine::Receive();
    Assert(object.retrieve(@monitor));

    

    //And the special value for this coroutine

    object = Coroutine::Receive();
    int val = 0;
    Assert(object.retrieve(val));

    //And finally receive another couroutine
    @object = Coroutine::Receive();
    Assert(object.retrieve(@myroutine));

    monitor.values.insertLast(val);

    myroutine.run();

    monitor.values.insertLast(val+1);

    Coroutine::Yield();

    monitor.values.insertLast(val+2);
}


[Test]
void TestPassingCoroutinesRecursive()
{
    CoroutineMonitor monitor;

    Coroutine@ crt1 = Coroutine::CreateCoroutine(@passingCoroutinesFuncComplex);
    Coroutine@ crt2 = Coroutine::CreateCoroutine(@passingCoroutinesFuncComplex);


    Assert(crt1.isFinished() == false);
    Assert(crt2.isFinished() == false);

    crt1.send(any(@monitor));
    crt2.send(any(@monitor));

    crt1.send(any(100));
    crt2.send(any(200));

    crt1.send(any(@crt2));
    crt2.send(any(@crt1));

    crt1.run();

    Assert(crt1.isFinished());
    Assert(crt2.isFinished() == false);

    crt2.run();
    Assert(crt2.isFinished());

    //Assert the correct control flow
    Assert(monitor.values[0] == 100); //crt1 starts -> calls crt2.run
    Assert(monitor.values[1] == 200); //crt2 starts -> calls crt1.run
    Assert(monitor.values[2] == 101); //crt1 continues from run function -> yields
    Assert(monitor.values[3] == 201); //crt2 continues from run function -> yields
    Assert(monitor.values[4] == 102); //crt1 returns from the function
    Assert(monitor.values[5] == 202); //crt2.run() is called afterwards by the main thread
}


// Execute the above test in a coroutine

[Test]
void TestPassingCoroutinesRecursiveInCoroutine()
{

    Coroutine@ crt = Coroutine::CreateCoroutine(@TestPassingCoroutinesRecursive);
    crt.run();
    Assert(crt.isFinished() == true);
}

// The following test is mainly a memory leak test

[Test]
void TestPassingCoroutinesRecursiveMemoryLeak()
{
    CoroutineMonitor monitor;

    Coroutine@ crt1 = Coroutine::CreateCoroutine(@passingCoroutinesFuncComplex);
    Coroutine@ crt2 = Coroutine::CreateCoroutine(@passingCoroutinesFuncComplex);
    Coroutine@ crt3 = Coroutine::CreateCoroutine(@passingCoroutinesFuncComplex);


    Assert(crt1.isFinished() == false);
    Assert(crt2.isFinished() == false);
    Assert(crt3.isFinished() == false);

    crt1.send(any(@monitor));
    crt2.send(any(@monitor));
    crt3.send(any(@monitor));


    crt1.send(any(100));
    crt2.send(any(200));
    crt3.send(any(300));

    crt1.send(any(@crt2));
    crt2.send(any(@crt3));
    crt3.send(any(@crt1));

    //Send a bunch of data
    crt1.send(any(@crt2));
    crt3.send(any(@crt2));
    crt2.send(any(@crt3));
    crt1.send(any(@crt3));
    crt3.send(any(@crt1));
    crt2.send(any(@crt1));

    crt1.run();

    //Never finish the two other coroutines

    Assert(crt1.isFinished());
    Assert(crt2.isFinished() == false);
    Assert(crt3.isFinished() == false);
}


[Test]
void TestYieldFromMainThreadFails()
{
    AssertThrowsAfterThis();
    Coroutine::Yield();
}

[Test]
void TestReceiveFromMainThreadFails()
{
    AssertThrowsAfterThis();
    Coroutine::Receive();
}


void receiveTwiceFunction()
{
    any@ object = Coroutine::Receive();
    any@ object2 = Coroutine::Receive();
}

[Test]
void TestReceiveEmptyMailboxFails()
{
    Coroutine@ crt = Coroutine::CreateCoroutine(@receiveTwiceFunction);
    crt.send(any(1));
    
    AssertThrowsAfterThis();
    crt.run();
}


[Test]
void TestNullCoroutineFunctionFails()
{
    AssertThrowsAfterThis();
    Coroutine@ crt = Coroutine::CreateCoroutine(null);
}

-- UNIT TESTS END

*/


