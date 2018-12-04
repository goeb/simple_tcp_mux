#ifndef _argv_h
#define _argv_h

#include <vector>
#include <string>
#include <stdarg.h>

/** Helper for passing command line arguments
 *
 *  Typical usage:
 *  Argv argv();
 *  argv.set("ls", "-l", 0);
 *  Subprocess::launch(argv.getv(), 0, 0);
 */
class Argv {
public:
    void set(const char *first, ...);
    void set(int argc, const char * const *argv);
    void append(const char *first, ...);
    char *const* getv() const;
    std::string to_string(const char *separator=", ") const;
    ~Argv();
    Argv(const Argv &other);
    Argv() {}

private:
    std::vector<char*> argv;
    void vappend(const char *first, va_list ap);
};

#endif // _argv_h
