#pragma once
#include "config.h"
#include "matlabWrapperDefine.h"
#include "DynamicLibrary.h"


class MatlabWrapper
{
	std::map<string, matlabWrapperCalulator>	m_modules;
	DynamicLibrary*							m_libraryManager;
	bool									m_loadedMatlabModule;

	static MatlabWrapper*					m_instance;
	static uint32							m_refInstance;

	MatlabWrapper();
public:
	static MatlabWrapper* getInstance();
	static void removeInstance();
	virtual ~MatlabWrapper();

	matlabWrapperCalulator registerMatlabModule(string moduleNamePath);

};
