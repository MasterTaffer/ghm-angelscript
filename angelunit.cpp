#include "angelunit.hpp"
#include <chrono>
#include <cassert>

#include <angelscript.h>
#include "coroutine.h"

#ifdef ANGELUNIT_XML_OUTPUT
#include <rapidxml.hpp>
#include <rapidxml_print.hpp>
#endif

namespace AngelUnit
{
    static void AU_Assert(bool value)
    {
        auto* ctx = asGetActiveContext();
        //if ctx is nullptr just crash
        //because in this function should get called
        //only from angelscript context

        assert(ctx != nullptr);
        ctx->SetUserData((void*)(0), AngelUnit_AssertThrows_ContextUserData);
        
        if (!value)
        {
            ctx->SetException("Assertion failed");
        }
    }
    
    static void AU_ShouldThrow()
    {
        
        auto* ctx = asGetActiveContext();
        //if ctx is nullptr just crash
        //because in this function should get called
        //only from angelscript context

        assert(ctx != nullptr);
        ctx->SetUserData((void*)(1), AngelUnit_AssertThrows_ContextUserData);
    }

    void RegisterAssertions(asIScriptEngine* ase)
    {
        int r;

        r = ase->RegisterGlobalFunction("void Assert(bool value)", asFUNCTION(AU_Assert), asCALL_CDECL);
        assert (r >= 0);
        (void)(r);
        
        
        r = ase->RegisterGlobalFunction("void AssertThrowsAfterThis()", asFUNCTION(AU_ShouldThrow), asCALL_CDECL);
        assert (r >= 0);
        (void)(r);
        
#ifdef NDEBUG
        (void)(r);
#endif

    }

    bool RunTest(TestResults* testResults, asIScriptFunction* testFunction, asIScriptContext* useContext, const char* testName, ASCoroutineStack* coroutineStack)
    {
        
        if (!useContext  || !testFunction)
        {
            testResults->errorMessage = "RunTest called with null testFunction or null useContext";
            return false;
        }

        if (testFunction->GetParamCount() != 0)
        {
            testResults->errorMessage = "testFunction has parameters";
            return false;
        }

        const char* test_name = testName;

        useContext->SetUserData((void*)(0), AngelUnit_AssertThrows_ContextUserData);

        if (!test_name)
            test_name = testFunction->GetName();

        auto* classType = testFunction->GetObjectType();

        testResults->name = test_name;

        int res = 0;

        if (classType)
        {
            if (classType->GetFactoryCount() == 0)
            {
                testResults->errorMessage = "testFunction is in a class with no default constructor";
                return false;
            }

            auto* factory = classType->GetFactoryByIndex(0);
            if (factory->GetParamCount() != 0)
            {
                testResults->errorMessage = "testFunction is in a class with no default constructor";
                return false;
            }

            res = useContext->Execute();
            if (res != asEXECUTION_FINISHED)
            {
                testResults->errorMessage = "testFunction class object construction failed";
                return false;
            }

            auto* object = *(asIScriptObject**)useContext->GetAddressOfReturnValue();
            object->AddRef();
            
            useContext->Prepare(testFunction);
            useContext->SetObject(object);	

            auto timer = std::chrono::high_resolution_clock::now();

            if (coroutineStack)
                res = coroutineStack->runMainThread(useContext);
            else
                res = useContext->Execute();

            auto measured = std::chrono::high_resolution_clock::now() - timer;
            long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(measured).count();
            testResults->runTime = microseconds;

            object->Release();
        }
        else
        {
            useContext->Prepare(testFunction);

            auto timer = std::chrono::high_resolution_clock::now();

            if (coroutineStack)
                res = coroutineStack->runMainThread(useContext);
            else
                res = useContext->Execute();

            auto measured = std::chrono::high_resolution_clock::now() - timer;
            long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(measured).count();
            testResults->runTime = microseconds;
        }

        testResults->testRan = true;
        
        size_t ud = (size_t)useContext->GetUserData(AngelUnit_AssertThrows_ContextUserData);
        
        if ((ud == 1 && res != asEXECUTION_EXCEPTION) || (ud == 0 && res != asEXECUTION_FINISHED))
        {
            const char* message = nullptr;
            const char* section = nullptr;
            int line = 0;
            int column = 0;
            if (res == asEXECUTION_EXCEPTION)
            {
                message = useContext->GetExceptionString();
                line = useContext->GetExceptionLineNumber(&column, &section);
            }
            else
            {
                if (ud == 1)
                    message = "No exception thrown";
                else
                    message = "Context aborted or suspended";
            }

            if (!message)
                message = "Unknown error";
            if (!section)
                section = "unknown";
            
            testResults->errorMessage = message;
            testResults->errorSection = section;
            testResults->errorLine = line;
            testResults->errorColumn = column;
            return false;
        }

        testResults->success = true;
        return true;

    }

    #ifdef ANGELUNIT_XML_OUTPUT
    
    void GenerateJUnitXMLForSuite(const TestSuite& testSuite, rapidxml::xml_document<char>& doc, rapidxml::xml_node<char>* rootnode)
    {
        auto suitenode = doc.allocate_node(rapidxml::node_element, "testsuite");
        rootnode->append_node(suitenode);

        
        rapidxml::xml_attribute<> *attr;
        int failures = 0;
        int errors = 0;
        for (auto& test : testSuite.tests)
        {
            auto testnode = doc.allocate_node(rapidxml::node_element, "testcase");
            suitenode->append_node(testnode);

            attr = doc.allocate_attribute("name", doc.allocate_string(test.name.c_str()));
            testnode->append_attribute(attr);
            attr = doc.allocate_attribute("time", doc.allocate_string(std::to_string(double(test.runTime) / 1000000.0).c_str()));
            testnode->append_attribute(attr);

            if (!test.success)
            {
                if (!test.testRan)
                {
                    auto failurenode = doc.allocate_node(rapidxml::node_element, "error");
                    testnode->append_node(failurenode);
                    errors += 1;
                }
                else
                {
                    auto failurenode = doc.allocate_node(rapidxml::node_element, "failure");
                    testnode->append_node(failurenode);

                    attr = doc.allocate_attribute("message", doc.allocate_string(test.errorMessage.c_str()));
                    failurenode->append_attribute(attr);

                    std::string failString = test.errorSection + ":" +
                        std::to_string(test.errorLine) + ":" +
                        std::to_string(test.errorColumn) + ":" + test.errorMessage;

                    failurenode->value(doc.allocate_string(failString.c_str()));
                    failures += 1;
                }
            }
        }

        attr = doc.allocate_attribute("tests", doc.allocate_string(std::to_string(testSuite.tests.size()).c_str()));
        suitenode->append_attribute(attr);
        if (errors > 0)
        {
            attr = doc.allocate_attribute("errors", doc.allocate_string(std::to_string(errors).c_str()));
            suitenode->append_attribute(attr);
        }
        if (failures > 0)
        {
            attr = doc.allocate_attribute("failures", doc.allocate_string(std::to_string(failures).c_str()));
            suitenode->append_attribute(attr);
        }
        attr = doc.allocate_attribute("name", doc.allocate_string(testSuite.name.c_str()));
        suitenode->append_attribute(attr);
    }

    bool GenerateJUnitXML(const TestSuite& suite, std::basic_ostream<char>& output)
    {
        rapidxml::xml_document<char> doc;
        auto rootnode = doc.allocate_node(rapidxml::node_element, "testsuites");
        doc.append_node(rootnode);

        GenerateJUnitXMLForSuite(suite, doc, rootnode);
        
        output << doc;
        return true;
    }

    bool GenerateJUnitXML(const std::vector<TestSuite>& suites, std::basic_ostream<char>& output)
    {
        

        rapidxml::xml_document<char> doc;
        auto rootnode = doc.allocate_node(rapidxml::node_element, "testsuites");
        doc.append_node(rootnode);

        for (auto& testSuite : suites)
        {
            GenerateJUnitXMLForSuite(testSuite, doc, rootnode);
        }
        output << doc;
        return true;

        
        return false;
    }
    #endif
}
