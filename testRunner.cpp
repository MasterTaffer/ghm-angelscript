/*

    GHMAS AngleScript test runner. Used to run the tests embedded into
    .cpp files.
 
    Take a look at GHMASScriptInterface below to see how to use the GHMAS
    extensions.
    
    Also take a look at the .cpp files of the extensions as they have
    AngelScript tests embedded into them (in comments, search for 
    "UNIT TESTS").
*/

#include <angelscript.h>

#include "angelunit.hpp"
#include "coroutine.h"
#include "thread.h"
#include "random.h"
#include "stringutils.h"
#include "entity.h"
#include "contextpool.h"
#include "binarystreambuilder.h"

#include <scriptarray/scriptarray.h>

#include <scriptstdstring/scriptstdstring.h>
#include <scripthandle/scripthandle.h>
#include <scriptmath/scriptmath.h>
#include <scriptarray/scriptarray.h>
#include <scriptany/scriptany.h>
#include <scriptbuilder/scriptbuilder.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>


class AngelScriptInterface
{
public:

    virtual bool registerInterface(asIScriptEngine* engine)
    {
        return true;
    }

    virtual bool postBuild(asIScriptEngine* engine, CScriptBuilder* builder)
    {
        return true;
    }

    virtual bool runTest(
        AngelUnit::TestResults* results,
        asIScriptFunction* func,
        asIScriptContext* context)
    {
        return AngelUnit::RunTest(results, func, context, nullptr, nullptr);
    }

    virtual bool release(asIScriptEngine* engine)
    {
        return true;
    }

    virtual ~AngelScriptInterface()
    {

    }
};


void messageCallback(const asSMessageInfo *msg, void*)
{
    std::cout << msg->section << ":" << msg->row << ":" << msg->col << ": ";
    if (msg->type == asMSGTYPE_WARNING)
        std::cout << "Warning - ";
    else
        if (msg->type == asMSGTYPE_INFORMATION)
            std::cout << "Info - ";
     std::cout << msg->message << std::endl;
}


void exceptionCallback(asIScriptContext *ctx, void*)
{
    int c;
    const char * sn;
    for (size_t i = 0; i < ctx->GetCallstackSize(); i++)
    {
        auto ln = ctx->GetLineNumber(i);
        auto p = ctx->GetFunction(i);
        if (p)
            std::cout << "In '" << p->GetDeclaration() << "' line " << ln << std::endl;
        else
            std::cout << "In 'null' line " << ln << std::endl;
    }

    std::cout << "On line " << ctx->GetExceptionLineNumber(&c, &sn) << ": ";
    if (sn != nullptr)
        std::cout << sn << " ";
    std::cout << ctx->GetExceptionString();
    std::cout << std::endl;
}



//Contains all the logic required to get the GHMAS extensions to work
class GHMASScriptInterface : public AngelScriptInterface
{
    ASContextPool ctxpool;
    ASCoroutineStack crstack;
    ASECS::EntitySystemManager esm;
public:
    GHMASScriptInterface()
    {
        asPrepareMultithread();
    }
    
    bool registerInterface(asIScriptEngine* engine)
    {
        //This isn't 100% necessary - if there are no globally constructed
        //EntityMolds
        engine->SetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD, 0);

        //The context pool is somewhat unnecessary and only exists to provide
        //convenient exception callback setup for all contexts
        ctxpool.setExceptionCallback(asFUNCTION(exceptionCallback), nullptr, asCALL_CDECL);
        ctxpool.connect(engine);
        
        RegisterScriptArray(engine, true); 
        RegisterStdString(engine);
        RegisterScriptHandle(engine);
        RegisterScriptMath(engine);
        RegisterScriptAny(engine);
        RegisterRandom(engine);

        RegisterCoroutine(engine, &crstack);
        RegisterThread(engine);


        esm.setLogCallback([](void*, const char* msg, int level)
        {
            if (level != ASECS::EntitySystemManager::Info)
                std::cout << "ESM: " << msg << std::endl;
        }, nullptr);

        esm.registerEngine(engine);

        return true;
    }

    bool postBuild(asIScriptEngine* engine, CScriptBuilder* builder)
    {
        //Entity system must know of all Component classes
        esm.initEntityClasses(builder);
        builder->GetModule()->ResetGlobalVars();
        return true;
    }


    bool runTest(
        AngelUnit::TestResults* results,
        asIScriptFunction* func,
        asIScriptContext* context)
    {
        //Clear entity system state
        esm.getSystem()->clear();
        //Rebuild entity class tables
        esm.getSystem()->preallocate();
        return AngelUnit::RunTest(results, func, context, nullptr, &crstack);
    }

    bool release(asIScriptEngine* engine)
    {

        crstack.releaseResources();
        ctxpool.disconnect();
        esm.release();
        
        return true;
    }

    ~GHMASScriptInterface()
    {
        asUnprepareMultithread();
    }
};




int main(int argc, const char** argv)
{

    auto printUsage = []() -> int
    {
        std::cerr << "Usage: ";
        std::cerr << "astests ";

        #ifdef ANGELUNIT_XML_OUTPUT
        std::cerr << "[--junit XML_OUTPUT] ";
        #endif

        std::cerr << "INPUTS ...";
        std::cerr << std::endl;
        return 1;
    };

    #ifdef ANGELUNIT_XML_OUTPUT
    std::string junitOutput = "TEST-astests.xml";
    #endif

    std::vector<const char*> inputFiles;

    argc--; argv++;
    
    while (argc)
    {
        #ifdef ANGELUNIT_XML_OUTPUT
        if (std::strcmp(*argv, "--junit") == 0)
        {
            argc--; argv++;
            if (argc == 0)
                return printUsage();
            junitOutput = std::string(*argv);
        }
        else
            inputFiles.push_back(*argv);            
        #else
        inputFiles.push_back(*argv);
        #endif
        argc--; argv++;
    }

    if (inputFiles.size() == 0)
        return printUsage();    


    auto isCppSource = [](const char* str) -> bool
    {
        //See from the extension if we got a c++ source file

        size_t len = std::strlen(str);
        if (len > 4 && std::strcmp(str + (len - 4), ".cpp") == 0)
            return true;
        if (len > 4 && std::strcmp(str + (len - 4), ".hpp") == 0)
            return true;
        if (len > 2 && std::strcmp(str + (len - 2), ".h") == 0)
            return true;
        return false;
    };

    //Register AngelScript interface

    auto iface =
        std::unique_ptr<AngelScriptInterface>(new GHMASScriptInterface());
    

    auto* ase = asCreateScriptEngine();
    ase->SetMessageCallback(asFUNCTION(messageCallback), nullptr, asCALL_CDECL);
    AngelUnit::RegisterAssertions(ase);


    if (iface->registerInterface(ase) == false)
    {
        std::cerr << "Failed to initialize AngelScript application interface" << std::endl;
        return 4;
    }

    BinaryStreamBuilder builder;
    builder.StartNewModule(ase, "tests");

    std::string unitTestHeader = "-- UNIT TESTS -- ";
    std::string unitTestEnd = "-- UNIT TESTS END";
    std::string unitTestSplitter = "--";
    

    auto addUnitTestsFromCppSource = [&](const char* file) -> int
    {
        int sections = 0;
        std::ifstream input(file);
        
        std::string line;

        std::stringstream buffer;
        std::string sectionName;
        int startLineNum = 0;

        bool readingToMemory = false;

        int lineNum = 0;
        while (std::getline(input, line))
        {
            lineNum++;
            if (readingToMemory)
            {
                if (line.compare(0, unitTestEnd.size(), unitTestEnd) == 0)
                {
                    buffer << "}\n";
                    readingToMemory = false;
                    builder.AddSectionFromMemory(
                        sectionName.c_str(),
                        buffer.str().c_str(),
                        0,
                        startLineNum - 1);
                    readingToMemory = false;
                    sections += 1;
                }
                else
                {
                    buffer << line << "\n";
                }
            }
            else
            if (line.compare(0, unitTestHeader.size(), unitTestHeader) == 0)
            {
                std::string out;
                std::stringstream ss(line);
                ss >> out; //--
                ss >> out; //UNIT
                ss >> out; //TESTS
                ss >> out; //--
                ss >> out;
                if (out != "AngelScript")
                    continue;
                ss >> out;
                if (out != "--")
                    continue;

                ss >> out; //suite name
                readingToMemory = true;
                sectionName = std::string(file) + "_" + out;

                //Put the tests into a namespace
                buffer << "namespace " << out << " { \n";
                startLineNum =lineNum;
            }
        }
        return sections;
    };

    for (auto& str : inputFiles)
    {
        if (isCppSource(str))
        {
            addUnitTestsFromCppSource(str);
        }
        else
        {
            int r = builder.AddSectionFromFile(str);
            if (r < 0)
            {
                std::cerr << "Failed to add script section from file \"" << str << "\"" << std::endl;
                return 6;
            }
        }
    }

    int r = builder.BuildModule();
    if (r < 0)
    {
        ase->ShutDownAndRelease();
        std::cerr << "Failed to build module" << std::endl;
        return 5;   
    }

    if (iface->postBuild(ase, &builder) == false)
    {
        ase->ShutDownAndRelease();
        std::cerr << "Application interface" << std::endl;
        return 4;
    }

    std::unordered_map<std::string, size_t> suiteIndices;
    std::vector<AngelUnit::TestSuite> suites;
    
    auto getSuite = [&](const std::string& name) -> AngelUnit::TestSuite&
    {
        auto it = suiteIndices.find(name);
        if (it == suiteIndices.end())
        {
            AngelUnit::TestSuite ts;
            ts.name = name;
            suiteIndices[name] = suites.size();
            suites.push_back(std::move(ts));

            return *(suites.rbegin());
        }
        return suites[it->second];

    };

    bool allSuccess = true;

    int i = 0;
    int fails = 0;
    std::cout << "Running tests" << std::endl;

    auto* ctx = ase->RequestContext();

    unsigned int cnt = builder.GetModule()->GetFunctionCount();
    for (unsigned int a = 0; a < cnt; ++a)
    {
        auto* func = builder.GetModule()->GetFunctionByIndex(a);

        //Check if we got a [Test]
        if (std::strcmp(
            builder.GetMetadataStringForFunc(func),
            "Test") != 0)
            continue;

        std::string testName;
        testName = func->GetName();

        std::string suiteName;
        auto* classType = func->GetObjectType();
        if (classType)
        {
            suiteName = classType->GetName();
        }
        else
        {
            if (func->GetNamespace())
            {
                suiteName = func->GetNamespace();
            }
            if (suiteName == "")
            {
                suiteName = func->GetModuleName();
            }
        }

        std::cout << "Running \"" << suiteName << "::" << testName << "\"" << std::endl;
        auto& suite = getSuite(suiteName);

        suite.tests.push_back(AngelUnit::TestResults());

        AngelUnit::TestResults& tr = *(suite.tests.rbegin());

        bool success = iface->runTest(&tr, func, ctx);
        allSuccess = success & allSuccess;

        if (success)
            std::cout << "OK ";
        else
        {
            std::cout << "FAILED ";
            fails += 1;
        }

        std::cout << "\"" << suiteName << "::" << testName << "\"" << std::endl;
        i+= 1;


    }
    ase->ReturnContext(ctx);

    //If there are no tests, don't output
    if (i != 0)
    {
        #ifdef ANGELUNIT_XML_OUTPUT
        std::ofstream file = std::ofstream(junitOutput);
        GenerateJUnitXML(suites, file);
        #endif
        
        std::cout << "Ran " << i << " tests ";
        if (fails)
        {
            std::cout << "with " << fails << " failure(s)" << std::endl;
        }
        else
            std::cout << "with no failures" << std::endl;
    }
    else
    {
        std::cout << "No tests ran" << std::endl;
    }

    if (!iface->release(ase))
        std::cerr << "Failed to release AngelScript application interface" << std::endl;

    ase->ShutDownAndRelease();
    
    if (fails)
        return 2;
    if (i == 0)
        return 3;
    return 0;
}
