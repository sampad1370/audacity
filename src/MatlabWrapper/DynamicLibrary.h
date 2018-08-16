#pragma once
#include "config.h"


class DynamicLibrary
{
	map<string, pair<void*, map<string, void*>>> m_modules;
	static DynamicLibrary* m_instance;

	DynamicLibrary();
public:
	static DynamicLibrary*	getInstance();
	~DynamicLibrary();

	bool registerModule(string name);
	void* getFunctionOfModule(string moduleName, string functionName);

};
