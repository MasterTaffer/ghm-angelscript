#pragma once
#include <angelscript.h>
#include <scriptbuilder/scriptbuilder.h>
#include <cstdint>

/*! \brief Binary bytecode saving/loading extension to CScriptBuilder

    Provides SaveModule and LoadModule functions for convenient bytecode
    saving and loading. Also when AS_PROCESS_METADATA is 1, metadata is saved
    with the bytecode as well. After loading a module previously saved
    with SaveModule, the metadata will be available to use via the normal
    CScriptBuilder interface.
*/
class BinaryStreamBuilder : public CScriptBuilder
{
#if AS_PROCESS_METADATA == 1

    bool errorInLoad;
    void WriteString(asIBinaryStream* out, const std::string& str);
    std::string ReadString(asIBinaryStream* in);

    void WriteInteger(asIBinaryStream* out, int integer);
    int ReadInteger(asIBinaryStream* in);

    void WriteByte(asIBinaryStream* out, uint8_t t);
    uint8_t ReadByte(asIBinaryStream* in);
    
    void SaveMetadata(asIBinaryStream* out);
    int LoadMetadata(asIBinaryStream* in);
#endif
public:

    void SaveModule(asIBinaryStream* out);
    int LoadModule(asIScriptEngine* engine, const char* name, asIBinaryStream* in);
};
