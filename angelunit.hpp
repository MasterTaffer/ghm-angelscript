#pragma once
#include <angelscript.h>
#include <iostream>
#include <string>
#include <vector>

//Define ANGELUNIT_XML_OUTPUT if you have rapidxml for automatic JUnit style
//test result output



class ASCoroutineStack;

//! The AngelScript test framework
namespace AngelUnit
{
    constexpr static const int AngelUnit_AssertThrows_ContextUserData = 581;
    
    //! Register common Assert functions to asIScriptEngine
    void RegisterAssertions(asIScriptEngine*);

    //! Struct representing the results of a single test
    struct TestResults
    {
        bool testRan = false;
        bool success = false;
        std::string errorMessage;
        std::string errorSection;
        int errorLine = 0;
        int errorColumn = 0;
        long long runTime = 0;
        std::string name;
    };

    /*! \brief Function to run a single test
     * 
     * This function will run a single AngelScript function as a test.
     * The function should not have any parameters and if the function is an
     * object method, the object should have a default constructor.
     * 
     * The useContext parameter should be a valid AngelScript context in 
     * unprepared state. This context will be used to run the test.
     * 
     * \param testResults output TestResults instance
     * \param testFunction the test to be run
     * \param useContext the context to use in running the test
     * \param testName the name of the test, may be nullptr
     * \param coroutineStack the ASCoroutineStack to use, may be nullptr
     * \return true on test success, false on failure
     */
    bool RunTest(
        TestResults* testResults,
        asIScriptFunction* testFunction,
        asIScriptContext* useContext,
        const char* testName = nullptr,
        ASCoroutineStack* coroutineStack = nullptr);

    //! Struct representing a collection of tests
    struct TestSuite
    {
        std::vector<TestResults> tests;
        std::string name;
    };
    
    #ifdef ANGELUNIT_XML_OUTPUT    
    //! Function to generate a JUnit XML format output from test suites
    bool GenerateJUnitXML(const std::vector<TestSuite>& suites, std::basic_ostream<char>& output);
    
    //! Function to generate a JUnit XML format output from a test suite
    bool GenerateJUnitXML(const TestSuite& suite, std::basic_ostream<char>& output);

    #endif

}
