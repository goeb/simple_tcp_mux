#ifndef _logging_h
#define _logging_h

#include <stdio.h>

extern int Verbose;

#define LOG(...) { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); }
#define LOG_LEVEL(_level, ...) { fprintf(stderr, "%s: ", _level); LOG(__VA_ARGS__); }

#define LOG_ERROR(...) do { LOG_LEVEL("ERROR", __VA_ARGS__); } while (0)
#define LOG_INFO(...) do { LOG(__VA_ARGS__); } while (0)
#define LOG_VERBOSE(...) do { if (Verbose) LOG(__VA_ARGS__); } while (0)

inline void log_set_verbose(int v) { Verbose = v; }


#endif // _logging_h
