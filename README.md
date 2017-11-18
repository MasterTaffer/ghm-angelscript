# GHM AngelScript extensions

Bunch of extensions used by the [Coppery game engine](https://github.com/MasterTaffer/Coppery).
Many of these depend on the standard AngelScript add-ons such as ScriptBuilder,
ScriptAny and ScriptArray. The extensions are not dependent on each other.

Most of the extensions have AngelScript examples embedded into the C++ source file.
Search for *UNIT TESTS* in the sources.

Download AngelScript and the standard add-ons from [AngelScript homepage](http://www.angelcode.com/angelscript/).

Compile these with C++11.

## Test runner

testrunner.cpp provides a simple test running utility for the extensions below.
It also shows an example how the application should use the extensions. 

**angelunit.cpp**

Barebones AngelScript unit test framework for the extensions. Is capable of
outputting somewhat JUnit style XML test result output, if ANGELUNIT_XML_OUTPUT
is defined and rapidxml headers are available.

**Compilation & Running**

Link with AngelScript and AngelScript standard extensions. The extensions below
should be included as well.

After compiling you may run the tests from extensions with

    ./testrunner entity.cpp coroutine.cpp ...
    
The testrunner will run all functions annotated with metadata [Test].
If you want to output test results to an XML file, compile with ANGELUNIT_XML_OUTPUT
and the testrunner will by default output results to *TEST-astests.xml*.


## Extensions

**BinaryStreamBuilder:**

Extension to the CScriptBuilder add on, adds support for loading and saving bytecode with
CScriptBuilder metadata. After BinaryStreamBuilder LoadModule is called, all the 
metadata saved with SaveModule is loaded and ready to use via the normal
CScriptBuilder metadata query methods.

**Coroutines:**

Support for cooperatively multitasked co-routines.

Application constructs a ASCoroutineStack and registers the interface with it.
Every script running coroutines should be executed with ASCoroutineStack 
runMainThread().

See coroutine.cpp for example usage.

**Context pool:**

A simple context pool that supports automatic exception callback setting for
all contexts.

**Entity:**

Implements pseudo-entity-component system to AngelScript.

Contains a lot of functionality, yet to be documented. See entity.cpp for
simple tests and the Coppery engine
[example game](https://github.com/MasterTaffer/Coppery/tree/master/bin/data/angelscript)
source code for example usage. 

**Random:**

Seeded random generator for AngelScript. Supports both manual seeding
and cloning the random generator state.

See random.cpp for example usage.

**Thread:**

Support for threads and simple inter-thread communication with message queues.

See thread.cpp for example usage.

