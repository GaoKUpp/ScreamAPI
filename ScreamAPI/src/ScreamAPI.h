#pragma once
#include "pch.h"
#include "Config.h"
#include <Overlay_types.h>
#include <eos-sdk\eos_sdk.h>

namespace ScreamAPI{

extern HMODULE thisDLL;
extern HMODULE originalDLL;
void init(HMODULE hModule);
void destroy();

class FunctionNotFoundException : public std::exception{};

// Have to define template function in header (-_-)
template <typename RetType, typename... ArgTypes>
struct proxyTraits{
	using funcType = RetType(EOS_CALL*)(ArgTypes...);
};

// A function that returns a type-safe reference to the requested EOS SDK function
template <typename RetType, typename... ArgTypes>
auto proxyFunction(RetType(EOS_CALL*)(ArgTypes...), LPCSTR rawFunctionName){
	using funcType = typename proxyTraits<RetType, ArgTypes...>::funcType;

#ifdef _WIN64
	std::string functionName = rawFunctionName;
#else
	// Need to decorate function name according to __stdcall convention:
	// https://docs.microsoft.com/en-us/cpp/build/reference/decorated-names?view=vs-2019#FormatC
	std::stringstream ss;
	ss << "_" << rawFunctionName << "@" << 4 * sizeof...(ArgTypes);
	std::string functionName = ss.str();
#endif
	// Get C-style pointer to function
	auto funcPtr = GetProcAddress(originalDLL, functionName.c_str());
	if(funcPtr){
		Logger::debug("Successfully proxied function: %s", functionName.c_str());
		// Return type-safe version of that function pointer
		return reinterpret_cast<funcType>(funcPtr);
	} else{
		Logger::error("Failed to proxy function: %s", functionName.c_str());
		throw FunctionNotFoundException();
	}

}

struct OriginalDataContainer{
	void* originalClientData;
	void (*originalCompletionDelegate)(const void*);
	OriginalDataContainer(void* clientData, void* completionDelegate) {
		originalClientData = clientData;
		reinterpret_cast<void*&>(originalCompletionDelegate) = completionDelegate;
	}
};

template <typename T>
void proxyCallback(const T* Data, void* const* clientData, std::function<void(T*)> customCallback){
	auto container = reinterpret_cast<OriginalDataContainer*>(*clientData);

	// Restore original client data
	auto mClientData = const_cast<void**>(clientData);
	*mClientData = container->originalClientData;

	// Call custom our callback
	T* mData = const_cast<T*>(Data);
	customCallback(mData);

	// Call original completion delegate with our modified data
	container->originalCompletionDelegate(Data);

	// Free the heap
	delete container;
}

#define EOS_IMPLEMENT_FUNC(function, ...)								\
	Logger::debug(__func__);											\
	static auto proxy = ScreamAPI::proxyFunction(&function, __func__);	\
	return proxy(__VA_ARGS__);

}
