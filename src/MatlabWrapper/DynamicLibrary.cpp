#include "DynamicLibrary.h"


DynamicLibrary* DynamicLibrary::m_instance = nullptr;

DynamicLibrary::DynamicLibrary()
{

}

DynamicLibrary::~DynamicLibrary()
{

}

bool DynamicLibrary::registerModule(string name)
{
	if (name == "")
		return false;
	if (m_modules.find(name) != m_modules.end())
		return true;
	HINSTANCE hGetProcIDDLL = LoadLibraryA(name.data());
	if (!hGetProcIDDLL)
	{
		cerr << "Error : Could not load the dynamic library '" << name << "'.";
		return false;
	}
	m_modules.insert(pair<string, pair<void*, map<string, void*>>>(name, { hGetProcIDDLL ,{} }));
	return true;
}

void* DynamicLibrary::getFunctionOfModule(string moduleName, string functionName)
{
	if (moduleName == "" || functionName == "")
		return nullptr;
	if (m_modules.find(moduleName) == m_modules.end())
	{
		if (!registerModule(moduleName))
			return nullptr;
	}
	auto& functionsList = m_modules[moduleName].second;
	auto hGetProcIDDLL = (HINSTANCE)m_modules[moduleName].first;
	if (functionsList.find(functionName) != functionsList.end())
		return functionsList[functionName];
	void* funci = GetProcAddress(hGetProcIDDLL, functionName.data());
	if (!funci) {
		cerr << "Could not locate the function '" << functionName << "' in Module '" << moduleName << "'.";
		return nullptr;
	}
	functionsList.insert(pair < string, void*>(functionName, funci));
	return funci;
}

DynamicLibrary * DynamicLibrary::getInstance()
{
	if (m_instance == nullptr)
	{
		m_instance = new DynamicLibrary();
	}
	return m_instance;
}