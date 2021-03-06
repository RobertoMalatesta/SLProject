//#############################################################################
//  File:      SL/stdafx.h
//  Author:    Marcus Hudritsch
//  Date:      July 2014
//  Codestyle: https://github.com/cpvrlab/SLProject/wiki/Coding-Style-Guidelines
//  Copyright: Marcus Hudritsch
//             This software is provide under the GNU General Public License
//             Please visit: http://opensource.org/licenses/GPL-3.0
//  Purpose:   Include file for standard system include files, or project
//             specific include files that are used frequently, but are changed
//             infrequently. You must set the property C/C++/Precompiled Header
//             as "Use Precompiled Header"
//#############################################################################

#ifndef STDAFX_H
#define STDAFX_H

#define _USE_MATH_DEFINES
//#define NOMINMAX

// Include standard C++ libraries
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>
//-----------------------------------------------------------------------------
// Include standard C libraries
#include <assert.h>   // for debug asserts
#include <float.h>    // for defines like FLT_MAX & DBL_MAX
#include <limits.h>   // for defines like UINT_MAX
#include <math.h>     // for math functions
#include <stdio.h>    // for the old ANSI C IO functions
#include <stdlib.h>   // srand, rand
#include <string.h>   // for string functions
#include <sys/stat.h> // for file info used in SLUtils
#include <time.h>     // for clock()
//-----------------------------------------------------------------------------
// Core header files used by all files
#include <SL.h>
#include <SLEnums.h>
#include <SLFileSystem.h>
#include <SLGLState.h>
#include <SLMat3.h>
#include <SLMat4.h>
#include <SLMath.h>
#include <SLObject.h>
#include <SLPlane.h>
#include <SLQuat4.h>
#include <SLRect.h>
#include <SLTimer.h>
#include <SLUtils.h>
#include <SLVec2.h>
#include <SLVec3.h>
#include <SLVec4.h>
#include <SLVector.h>
//-----------------------------------------------------------------------------
#endif
