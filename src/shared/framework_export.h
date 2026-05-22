#pragma once

#ifdef FRAMEWORK_EXPORTS
#define FRAMEWORK_API __declspec(dllexport)
#elif defined(FRAMEWORK_IMPORTS)
#define FRAMEWORK_API __declspec(dllimport)
#else
#define FRAMEWORK_API
#endif
