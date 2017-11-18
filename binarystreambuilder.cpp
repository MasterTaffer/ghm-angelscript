#include "binarystreambuilder.h"
#include <iostream>

#if AS_PROCESS_METADATA == 1

void BinaryStreamBuilder::WriteString(asIBinaryStream* out, const std::string& str)
{
    WriteInteger(out, str.size());
    for (size_t i = 0; i < str.size(); i++)
        WriteByte(out, static_cast<uint8_t>(str[i]));

}

std::string BinaryStreamBuilder::ReadString(asIBinaryStream* in)
{
    std::string s;

    int size = ReadInteger(in);
    if (size < 0)
    {
        errorInLoad = true;
        return "";
    }

    s.resize(size);

    for (size_t i = 0; i < s.size(); i++)
        s[i] = static_cast<char>(ReadByte(in));
    return s;
}

void BinaryStreamBuilder::WriteInteger(asIBinaryStream* out, int integer)
{
    WriteByte(out, (integer >> 24) & 0xFF);
    WriteByte(out, (integer >> 16) & 0xFF);
    WriteByte(out, (integer >> 8) & 0xFF);
    WriteByte(out, (integer) & 0xFF);
}

int BinaryStreamBuilder::ReadInteger(asIBinaryStream* in)
{
    int v = 0;
    v += ReadByte(in) << 24;
    v += ReadByte(in) << 16;
    v += ReadByte(in) << 8;
    v += ReadByte(in);
    return v;
}

void BinaryStreamBuilder::SaveMetadata(asIBinaryStream* out)
{
    WriteInteger(out, foundDeclarations.size());
    for (auto& t : foundDeclarations)
    {

        WriteString(out, t.metadata);
        WriteString(out, t.declaration);
        WriteInteger(out, t.type);
        WriteString(out, t.parentClass);
        WriteString(out, t.nameSpace);
    }	
}

int BinaryStreamBuilder::LoadMetadata(asIBinaryStream* in)
{
    errorInLoad = false;
    int mc = ReadInteger(in);
    if (errorInLoad) return -1;
    while (mc > 0)
    {
        auto metadata = ReadString(in);
        if (errorInLoad) return -1;
        auto declaration = ReadString(in);
        if (errorInLoad) return -1;
        auto type = ReadInteger(in);
        if (errorInLoad) return -1;
        auto parentClass = ReadString(in);
        if (errorInLoad) return -1;
        auto nameSpace = ReadString(in);
        if (errorInLoad) return -1;
        foundDeclarations.push_back(SMetadataDecl(metadata, declaration, type, parentClass, nameSpace));
        --mc;
    }		
    return 0;
}

uint8_t BinaryStreamBuilder::ReadByte(asIBinaryStream* in)
{
    uint8_t t;
    in->Read((void*)&t, 1);
    return t;
}

void BinaryStreamBuilder::WriteByte(asIBinaryStream* out, uint8_t t)
{
    out->Write((const void*)&t, 1);
}

#endif

void BinaryStreamBuilder::SaveModule(asIBinaryStream* out)
{
    if (module)
    {
        if (module->SaveByteCode(out) < 0)
            return;

        #if AS_PROCESS_METADATA == 1
        SaveMetadata(out);
        #endif
    }

}

int BinaryStreamBuilder::LoadModule(asIScriptEngine* ase, const char* name, asIBinaryStream* in)
{
    if(!ase)
        return -1;

    engine = ase;
    module = engine->GetModule(name, asGM_ALWAYS_CREATE);
    if(!module)
        return -1;

    ClearAll();

    if (module->LoadByteCode(in) < 0)
        return -1;
    
    #if AS_PROCESS_METADATA == 1

    if (LoadMetadata(in) < 0)
    {
        engine->WriteMessage("", 0, 0, asMSGTYPE_ERROR, (std::string("Failed to load metadata for module ")+name+": corrupted byte stream").c_str());
        return -1;
    }

    //Following (almost) directly copied from AngelScript standard extension
    //CScriptBuilder::Build() (see scriptbuilder/scriptbuilder.cpp)

    //CScriptBuilder::Build method in parses the metadata foundDeclarations
    //array to more usable format. However, the Build method could not be
    //reused because it builds the module - and this discards the loaded
    //bytecode, apparently.

    //Small modifications were made for error reporting instead of 
    //assertion, aborting the whole program on failure. Corrupted
    //metadata in the binary stream may result in these errors.

    //--------------------------

    using namespace std;
    int invalidMetadata = 0;

    // After the script has been built, the metadata strings should be
    // stored for later lookup by function id, type id, and variable index
    for( int n = 0; n < (int)foundDeclarations.size(); n++ )
    {
        SMetadataDecl *decl = &foundDeclarations[n];
        module->SetDefaultNamespace(decl->nameSpace.c_str());
        if( decl->type == 1 )
        {
            // Find the type id
            int typeId = module->GetTypeIdByDecl(decl->declaration.c_str());
            
            if( typeId >= 0 )
                typeMetadataMap.insert(map<int, string>::value_type(typeId, decl->metadata));
            else
                invalidMetadata += 1;
        }
        else if( decl->type == 2 )
        {
            if( decl->parentClass == "" )
            {
                // Find the function id
                asIScriptFunction *func = module->GetFunctionByDecl(decl->declaration.c_str());
                
                if( func )
                    funcMetadataMap.insert(map<int, string>::value_type(func->GetId(), decl->metadata));
                else
                    invalidMetadata += 1;
            }
            else
            {
                // Find the method id
                int typeId = module->GetTypeIdByDecl(decl->parentClass.c_str());
                if (typeId <= 0)
                {
                    invalidMetadata += 1;
                    continue;
                }

                map<int, SClassMetadata>::iterator it = classMetadataMap.find(typeId);
                if( it == classMetadataMap.end() )
                {
                    classMetadataMap.insert(map<int, SClassMetadata>::value_type(typeId, SClassMetadata(decl->parentClass)));
                    it = classMetadataMap.find(typeId);
                }

                asITypeInfo *type = engine->GetTypeInfoById(typeId);
                asIScriptFunction *func = type->GetMethodByDecl(decl->declaration.c_str());
                
                if( func )
                    it->second.funcMetadataMap.insert(map<int, string>::value_type(func->GetId(), decl->metadata));
                else
                    invalidMetadata += 1;
            }
        }
        else if( decl->type == 4 )
        {
            if( decl->parentClass == "" )
            {
                // Find the global virtual property accessors
                asIScriptFunction *func = module->GetFunctionByName(("get_" + decl->declaration).c_str());
                if( func )
                    funcMetadataMap.insert(map<int, string>::value_type(func->GetId(), decl->metadata));
                func = module->GetFunctionByName(("set_" + decl->declaration).c_str());
                if( func )
                    funcMetadataMap.insert(map<int, string>::value_type(func->GetId(), decl->metadata));
            }
            else
            {
                // Find the method virtual property accessors
                int typeId = module->GetTypeIdByDecl(decl->parentClass.c_str());
                if (typeId <= 0)
                {
                    invalidMetadata += 1;
                    continue;
                }
                map<int, SClassMetadata>::iterator it = classMetadataMap.find(typeId);
                if( it == classMetadataMap.end() )
                {
                    classMetadataMap.insert(map<int, SClassMetadata>::value_type(typeId, SClassMetadata(decl->parentClass)));
                    it = classMetadataMap.find(typeId);
                }

                asITypeInfo *type = engine->GetTypeInfoById(typeId);
                asIScriptFunction *func = type->GetMethodByName(("get_" + decl->declaration).c_str());
                if( func )
                    it->second.funcMetadataMap.insert(map<int, string>::value_type(func->GetId(), decl->metadata));
                func = type->GetMethodByName(("set_" + decl->declaration).c_str());
                if( func )
                    it->second.funcMetadataMap.insert(map<int, string>::value_type(func->GetId(), decl->metadata));

            }
        }
        else if( decl->type == 3 )
        {
            if( decl->parentClass == "" )
            {
                // Find the global variable index
                int varIdx = module->GetGlobalVarIndexByName(decl->declaration.c_str());
                if( varIdx >= 0 )
                    varMetadataMap.insert(map<int, string>::value_type(varIdx, decl->metadata));
                else
                    invalidMetadata += 1;
            }
            else
            {
                int typeId = module->GetTypeIdByDecl(decl->parentClass.c_str());
                if (typeId <= 0)
                {
                    invalidMetadata += 1;
                    continue;
                }
                // Add the classes if needed
                map<int, SClassMetadata>::iterator it = classMetadataMap.find(typeId);
                if( it == classMetadataMap.end() )
                {
                    classMetadataMap.insert(map<int, SClassMetadata>::value_type(typeId, SClassMetadata(decl->parentClass)));
                    it = classMetadataMap.find(typeId);
                }

                // Add the variable to class
                asITypeInfo *objectType = engine->GetTypeInfoById(typeId);
                int idx = -1;

                // Search through all properties to get proper declaration
                for( asUINT i = 0; i < (asUINT)objectType->GetPropertyCount(); ++i )
                {
                    const char *name;
                    objectType->GetProperty(i, &name);
                    if( decl->declaration == name )
                    {
                        idx = i;
                        break;
                    }
                }

                // If found, add it
                if( idx >= 0 )
                    it->second.varMetadataMap.insert(map<int, string>::value_type(idx, decl->metadata));
                else
                    invalidMetadata += 1;
            }
        }
    }
    module->SetDefaultNamespace("");

    if (invalidMetadata > 0)
    {
        engine->WriteMessage("", 0, 0, asMSGTYPE_WARNING, (std::string("Encountered invalid metadata when loading module ")+name).c_str());	
        return -1;
    }
    #endif
    return 0;
}	
