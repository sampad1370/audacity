#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <ctime>
#include <cstdlib>
#include <cassert>
#include <ccomplex>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <forward_list>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <mutex>
#include <map>
#include <queue>
#include <random>
#include <ratio>
#include <string>
#include <set>
#include <sstream>
#include <stdexcept>
#include <typeinfo>
#include <thread>
#ifdef __unix
#include <unistd.h>
#endif
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(WIN32)||defined(WIN64)
#include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glu.h>
//#include<GL/glext.h>
#if defined(debug)||defined(DEBUG)
#define QT_DEBUG_DRAW
#endif


#ifndef uint8
typedef uint8_t uint8;
#endif

#ifndef uint16
typedef uint16_t uint16;
#endif

#ifndef uint32
typedef uint32_t uint32;
#endif

#ifndef uint64
typedef uint64_t uint64;
#endif

#ifndef int8
typedef int8_t int8;
#endif

#ifndef int16
typedef int16_t int16;
#endif

#ifndef int32
typedef int32_t int32;
#endif

#ifndef int64
typedef int64_t int64;
#endif

#define SafeDelete(x) if(x) {delete x; x=nullptr;}
#define SafeDeleteArray(x) if(x) {delete[] x; x=nullptr;}

using namespace std;
