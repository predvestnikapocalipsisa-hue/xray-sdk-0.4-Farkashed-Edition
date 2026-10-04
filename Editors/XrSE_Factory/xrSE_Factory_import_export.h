#ifndef XRSE_FACTORY_IMPORT_EXPORTH
#define XRSE_FACTORY_IMPORT_EXPORTH

#if defined(XRSEFACTORY_EXPORTS) || defined(XR_SDK_RUNTIME_BUILD)
#define FACTORY_API __declspec(dllexport)
#else
#define FACTORY_API __declspec(dllimport)
#endif

namespace XrSE_Factory
{
	FACTORY_API ISE_Abstract *create_entity(LPCSTR section);
	FACTORY_API void destroy_entity(ISE_Abstract *&);
	FACTORY_API void initialize();
	FACTORY_API void destroy();
}

#endif