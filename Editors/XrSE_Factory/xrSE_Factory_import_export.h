#ifndef XRSE_FACTORY_IMPORT_EXPORTH
#define XRSE_FACTORY_IMPORT_EXPORTH

#if defined(XRSEFACTORY_EXPORTS) || defined(XR_SDK_RUNTIME_BUILD)
#define FACTORY_API __declspec(dllexport)
#else
#define FACTORY_API __declspec(dllimport)
#endif

struct ISE_Abstract;
class NET_Packet;
class CInifile;

namespace XrSE_Factory
{
	FACTORY_API ISE_Abstract *create_entity(LPCSTR section);
	FACTORY_API CLASS_ID compiled_entity_class(LPCSTR section);
	FACTORY_API ISE_Abstract *create_compiled_entity(LPCSTR section, CLASS_ID clsid);
	FACTORY_API BOOL read_compiled_entity(ISE_Abstract* entity, NET_Packet& packet);
	FACTORY_API void write_compiled_entity(ISE_Abstract* entity, NET_Packet& packet);
	FACTORY_API void destroy_entity(ISE_Abstract *&);
	FACTORY_API void initialize();
	FACTORY_API void destroy();
	FACTORY_API void SelectCompiledLevelConfig(const wchar_t* directory);
	FACTORY_API void RememberCompiledLevelConfig();
	FACTORY_API CInifile* OpenCompiledSimulation();
	FACTORY_API LPCSTR CompiledSourceValue(LPCSTR section, LPCSTR key);
	FACTORY_API LPCSTR CompiledNpcVisual(LPCSTR profile);
}

#endif
