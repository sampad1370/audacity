#include "MatlabWrapper.h"

int filter(unsigned int code, struct _EXCEPTION_POINTERS *ep) 
{

	puts("in filter.");

	if (code == EXCEPTION_ACCESS_VIOLATION) {

		puts("caught AV as expected.");

		return EXCEPTION_EXECUTE_HANDLER;

	}

	else {

		puts("didn't catch AV, unexpected.");

		return EXCEPTION_CONTINUE_SEARCH;

	};

}

bool safeCall(matlabWrapperInitialize initilaize)
{
	__try
	{
		return initilaize();
	}
	__except(filter(GetExceptionCode(), GetExceptionInformation()))
	{
		puts("in except");
	}
	return true;
}


MatlabWrapper* MatlabWrapper::m_instance = nullptr;

uint32 MatlabWrapper::m_refInstance = 0;

MatlabWrapper::MatlabWrapper()
{
	m_loadedMatlabModule = false;
	if (!mclInitializeApplication(nullptr, 0))
	{
		cerr << "Could not initialize the Matlab Core Module!";
		return;
	}
	m_loadedMatlabModule = true;
	m_libraryManager = DynamicLibrary::getInstance();
}

MatlabWrapper* MatlabWrapper::getInstance()
{
	if (m_instance == nullptr)
	{
		m_refInstance = 0;
		m_instance = new MatlabWrapper();
	}
	m_refInstance++;
	return m_instance;
}

void MatlabWrapper::removeInstance()
{
	if (m_refInstance > 0)
		m_refInstance--;
	if (m_refInstance <= 0 && m_instance != nullptr)
		SafeDelete(m_instance);
}

MatlabWrapper::~MatlabWrapper()
{
	if (m_loadedMatlabModule)
		mclTerminateApplication();
	m_loadedMatlabModule = false;
}

matlabWrapperCalulator MatlabWrapper::registerMatlabModule(string moduleNamePath)
{
	if (!m_loadedMatlabModule)
		return nullptr;
	try
	{
		if (moduleNamePath == "")
			return nullptr;
		if (m_libraryManager->registerModule(moduleNamePath))
         if (m_modules.find(moduleNamePath) != m_modules.end())
         {
            return m_modules[moduleNamePath];
         }
		auto pos=moduleNamePath.find_last_of('//');
		string moduleName = moduleNamePath.substr(pos + 1, moduleNamePath.size() - pos);
		pos = moduleName.find_first_of('.');
		moduleName = moduleName.substr(0, pos);
		auto initailize = (matlabWrapperInitialize)m_libraryManager->getFunctionOfModule(moduleNamePath, moduleName + "Initialize");
		if (initailize != nullptr)
		{
			try
			{
				if (safeCall(initailize))
				{
					moduleName[0] = toupper(moduleName[0]);
					auto matlabCalculator = (matlabWrapperCalulator)m_libraryManager->getFunctionOfModule(moduleNamePath,
						"mlf"+moduleName);
               if (matlabCalculator != nullptr)
               {
                  m_modules[moduleNamePath] = matlabCalculator;
                  return matlabCalculator;
               }
					else
					{
						moduleName[0] = tolower(moduleName[0]);
						auto matlabCalculator = (matlabWrapperCalulator)m_libraryManager->getFunctionOfModule(moduleNamePath,
							"?" + moduleName + "@@YAXHAEAVmwArray@@AEBV1@1@Z");
                  if (matlabCalculator != nullptr)
                  {
                     m_modules[moduleNamePath] = matlabCalculator;
                     return matlabCalculator;
                  }
						else
							cerr << "Could not find the Matlab Module Function '" << moduleName << "'";
					}
				}
				else
					cerr << "Could not initialize the Matlab Module '" << moduleNamePath << "'";
			}
			catch (exception e)
			{
				return nullptr;
			}
			catch (...)
			{
				return nullptr;
			}
		}
	}
	catch (exception e)
	{
		return nullptr;
	}
	catch (...)
	{
		return nullptr;
	}
	return nullptr;
}
