#pragma once
#include <iostream>
#include <angelscript.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <scriptany/scriptany.h>

// AngelScript threading interface

//! Context userdata index for current thread
const int ASThread_ContextUD = 550;

const int ASThread_EngineListUD = 551;

//! Mailbox class for inter thread communication
class ASMailbox
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<CScriptAny*> box;
    int dbg = 0;
public:

    //! Send data to the mailbox
    void send(CScriptAny* any);

    //! Wait and receive data
    CScriptAny* receiveForever();

    /*! Wait until timeout and receive data

        \param timeout timeout in milliseconds
    */
    CScriptAny* receive(uint64_t timeout);

    //! AngelScript GC enumerate references function
    void enumReferences(asIScriptEngine* engine);

    //! Get current size of the mailbox
    int boxSize();

    //! Release data in the mailbox
    void release();
};

class ASThread
{
    asIScriptEngine* engine = nullptr;
    asIScriptContext* context = nullptr;
    asIScriptFunction* func = nullptr;
    std::thread thread;
    std::mutex mutex;
    bool running = false;
    std::condition_variable finished_cv;
    static void static_internal_run(ASThread* t);
    void internal_run();
    int ref = 1;
    bool gcFlag = false;
public:
    //! Increment the reference counter
    void addRef();

    //! Decrement the reference counter
    void release();

    //! Stop the thread
    void suspend();



    //! Set GC Flag function for AngelScript garbage collector
    void setGCFlag();
    //! Get GC Flag function for AngelScript garbage collector
    bool getGCFlag();
    //! Get reference count function for AngelScript garbage collector
    int getRefCount();
    //! AngelScript GC enumerate references function
    void enumReferences(asIScriptEngine*);

    //! Data incoming to the thread
    ASMailbox incoming;

    //! Data outgoing from the thread
    ASMailbox outgoing;

    /*! Wait for the thread to terminate

        \return 0 the thread did not terminate, 1 if it did, -1 if the thread
        was already finished
    */
    int wait(uint64_t timeout);

    //! Send data to the thread
    void send(CScriptAny* any);

    //! Receive data from the thread
    CScriptAny* receiveForever();

    
    /*! Receive data from the thread with timeout

        \param timeout timeout in milliseconds
    */
    CScriptAny* receive(uint64_t timeout);

    //! Check if the thread is finished
    bool isFinished();

    /*! Run the thread

        \return true if thread was successfully started
    */
    bool run();

    /*! Constructor

        \param engine AngelScript engine
        \param func Function to run
    */
    ASThread(asIScriptEngine* engine, asIScriptFunction* func);

    //! Release all references held by the thread
    void releaseAllReferences(asIScriptEngine*);
};

//! Register thread interface into the engine
void RegisterThread(asIScriptEngine* ase);
