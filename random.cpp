#include "random.h"
#include <cassert>

static ASRandomGenerator* factoryRandomGenerator()
{
    return new ASRandomGenerator();
}

void RegisterRandom(asIScriptEngine* ase)
{
    int r;
    r = ase->RegisterObjectType("RandomGenerator", 0, asOBJ_REF);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("RandomGenerator", asBEHAVE_FACTORY, "RandomGenerator@ f()", asFUNCTION(factoryRandomGenerator), asCALL_CDECL);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("RandomGenerator", asBEHAVE_ADDREF, "void f()", asMETHOD(ASRandomGenerator, addRef), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectBehaviour("RandomGenerator", asBEHAVE_RELEASE, "void f()", asMETHOD(ASRandomGenerator, release), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("RandomGenerator", "void opAssign(const RandomGenerator&)", asMETHODPR(ASRandomGenerator, assign, (ASRandomGenerator*), void), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("RandomGenerator", "void seed(uint)", asMETHODPR(ASRandomGenerator, seed, (uint32_t), void), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("RandomGenerator", "void seed(uint[]&)", asMETHODPR(ASRandomGenerator, seed, (CScriptArray*), void), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("RandomGenerator", "int getI()", asMETHOD(ASRandomGenerator, getI), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("RandomGenerator", "uint getU()", asMETHOD(ASRandomGenerator, getU), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("RandomGenerator", "double getD()", asMETHOD(ASRandomGenerator, getD), asCALL_THISCALL);
    assert(r >= 0);

    r = ase->RegisterObjectMethod("RandomGenerator", "void seedFromTime()", asMETHOD(ASRandomGenerator, seedFromTime), asCALL_THISCALL);
    assert(r >= 0);
    
#ifdef NDEBUG
    (void)(r);
#endif
}

void ASRandomGenerator::addRef()
{
    asAtomicInc(ref);
}

void ASRandomGenerator::release()
{
    if (asAtomicDec(ref) <= 0)
        delete this;
}

ASRandomGenerator::ASRandomGenerator()
{
    seedFromTime();
}

void ASRandomGenerator::seedFromTime()
{
    seed(static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

uint32_t ASRandomGenerator::getU()
{
    return twister();
}

int32_t ASRandomGenerator::getI()
{
    return twister();
}

double ASRandomGenerator::getD()
{
    return ddist(twister);
}

void ASRandomGenerator::seed(uint32_t seed)
{
    twister.seed(seed);
}

void ASRandomGenerator::seed(CScriptArray * arr)
{
    if (arr->GetElementTypeId() != asTYPEID_UINT32)
    {
        auto* ctx = asGetActiveContext();
        ctx->SetException("ASRandomGenerator::Seed array element type not uint32");
        return;
    }
    std::vector<uint32_t> vec;
    vec.reserve(arr->GetSize());
    for (unsigned int i = 0; i < arr->GetSize(); i++)
    {
        vec.push_back(static_cast<uint32_t*>(arr->GetBuffer())[i]);
    }
    std::seed_seq seq(vec.begin(), vec.end());
    twister.seed(seq);
}

void ASRandomGenerator::assign(ASRandomGenerator * from)
{
    twister = from->twister;
}



/* Tests for the above

    These tests mainly test the seeding behaviour. No "deterministic
    randomness" tests, sorry.

-- UNIT TESTS -- AngelScript -- RandomGenerator 

    [Test]
    void RandomSeededTest()
    {
        RandomGenerator a, b;

        a.seed(24);
        b.seed(24);
        for (int i = 0; i < 10; i++)
        {
            Assert(a.getI() == b.getI());
            Assert(a.getU() == b.getU());
        }
    }

    [Test]
    void RandomCopy()
    {
        RandomGenerator a;
        a.seed(24);
        a.getI();
        a.getI();
        a.getI();

        RandomGenerator b = a;
        Assert(a.getI() == b.getI());
        Assert(a.getI() == b.getI());
        Assert(a.getI() == b.getI());
        Assert(a.getI() == b.getI());
    }


    [Test]
    void RandomLongSeed()
    {
        uint[] lseed1 = {2,5,7,8,9,1,2,5,4};

        RandomGenerator a;
        a.seed(2345);
        a.seed(lseed1);
        int v1 = a.getI();
        int v2 = a.getI();
        int v3 = a.getI();
        
        uint[] lseed2 = {2,5,7,8,9,1,2,5,4};
        RandomGenerator b;
        b.seed(1234);
        b.seed(lseed2);
        Assert(v1 == b.getI());
        Assert(v2 == b.getI());
        Assert(v3 == b.getI());
    }

-- UNIT TESTS END

*/

