#pragma once
#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <random>
#include <chrono>



/*! Random generator interface for AngelScript

    Internally uses c++ standard Mersenne Twister std::mt19937.
*/
class ASRandomGenerator
{
    std::mt19937 twister;
    std::uniform_real_distribution<double> ddist = std::uniform_real_distribution<double>(0.0, 1.0);
    int ref = 1;
public:

    //! Increment the reference counter
    void addRef();

    //! Decrement the reference counter
    void release();

    /*! Constructor that automatically seeds the generator with using the
        current	time.
    */
    ASRandomGenerator();

    //! Seed the generator using the current time
    void seedFromTime();

    //! Get random 32bit wide unsigned integer
    uint32_t getU();

    //! Get random 32bit wide signed integer
    int32_t getI();

    //! Get random double within range 0.0 - 1.0
    double getD();

    //! Seed the generator from a single value
    void seed(uint32_t seed);

    //! Seed the generator from an array of values
    void seed(CScriptArray* arr);

    //! Assignment operator 
    void assign(ASRandomGenerator* from);

};

void RegisterRandom(asIScriptEngine* ase);
