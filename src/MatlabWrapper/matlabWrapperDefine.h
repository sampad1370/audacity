#pragma once


#if defined(__cplusplus) && !defined(mclmcrrt_h) && defined(__linux__)
#  pragma implementation "mclmcrrt.h"
#endif
#include <mclmcrrt.h>
#include <mclcppclass.h>
#ifdef __cplusplus
extern "C" {
#endif

#if defined(__SUNPRO_CC)
/* Solaris shared libraries use __global, rather than mapfiles
 * to define the API exported from a shared library. __global is
 * only necessary when building the library -- files including
 * this header file to use the library do not need the __global
 * declaration; hence the EXPORTING_<library> logic.
 */

#ifdef EXPORTING_MATLABWRAPPER
#define PUBLIC_MATLABWRAPPER_C_API __global
#else
#define PUBLIC_MATLABWRAPPER_C_API /* No import statement needed. */
#endif

#define LIB_MATLABWRAPPER_C_API PUBLIC_MATLABWRAPPER_C_API

#elif defined(_HPUX_SOURCE)

#ifdef EXPORTING_MATLABWRAPPER
#define PUBLIC_MATLABWRAPPER_C_API __declspec(dllexport)
#else
#define PUBLIC_MATLABWRAPPER_C_API __declspec(dllimport)
#endif

#define LIB_MATLABWRAPPER_C_API PUBLIC_MATLABWRAPPER_C_API


#else

#define LIB_MATLABWRAPPER_C_API

#endif

/* This symbol is defined in shared libraries. Define it here
 * (to nothing) in case this isn't a shared library. 
 */
#ifndef LIB_MATLABWRAPPER_C_API 
#define LIB_MATLABWRAPPER_C_API /* No special import/export declaration */
#endif

/*extern LIB_MATLABWRAPPER_C_API*/
using matlabWrapperInitializeWithHandlers = bool /*MW_CALL_CONV*/ (*)(
       mclOutputHandlerFcn error_handler, 
       mclOutputHandlerFcn print_handler);

using matlabWrapperInitialize = bool /*MW_CALL_CONV*/ (*)(void);

/*extern LIB_MATLABWRAPPER_C_API*/
using matlabWrapperTerminate = void /*MW_CALL_CONV*/ (*)(void);



/*extern LIB_MATLABWRAPPER_C_API*/
using matlabWrapperPrintStackTrace = void /*MW_CALL_CONV*/ (*)(void);

/*extern LIB_MATLABWRAPPER_C_API*/
using mlxmatlabWrapper = bool /*MW_CALL_CONV*/ (*)(int nlhs, mxArray *plhs[], int nrhs, mxArray *prhs[]);


#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/* On Windows, use __declspec to control the exported API */
#if defined(_MSC_VER) || defined(__BORLANDC__)

#ifdef EXPORTING_MATLABWRAPPER
#define PUBLIC_MTLABWRAPPER_CPP_API __declspec(dllexport)
#else
#define PUBLIC_MATLABWRAPPER_CPP_API __declspec(dllimport)
#endif

#define LIB_MATLABWRAPPER_CPP_API PUBLIC_MATLABWRAPPER_CPP_API

#else

#if !defined(LIB_MATLABWRAPPER_CPP_API)
#if defined(LIB_MATLABWRAPPER_C_API)
#define LIB_MATLABWRAPPER_CPP_API LIB_MATLABWRAPPER_C_API
#else
#define LIB_MATLABWRAPPER_CPP_API /* empty! */ 
#endif
#endif

#endif

/*extern LIB_MATLABWRAPPER_CPP_API */
using matlabWrapperCalulator = void /*MW_CALL_CONV*/ (*)(int nargout, 
														mwArray& res, const mwArray& cleanSpeechInputWavFile,
														const mwArray& noiseSamplesWavFile);

#endif
