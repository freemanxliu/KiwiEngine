#pragma once

// Windows (includes wingdi.h which has WGL)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// glad must be included AFTER Windows.h (needs APIENTRY)
#include <glad/gl.h>

// Undefine Windows macros that clash
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
