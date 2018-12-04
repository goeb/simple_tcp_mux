#ifndef _subprocess_h
#define _subprocess_h

#include <string>
#include <vector>
#include <stdarg.h>

#include "argv.h"

enum StandardFd {
    SUBP_STDIN = 0,
    SUBP_STDOUT = 1,
    SUBP_STDERR = 2
};

enum PipeDirection {
    SUBP_READ = 0,
    SUBP_WRITE = 1
};

class Subprocess {

public:
    static Subprocess *launch(char *const argv[], char * const envp[], const char *dir);
    Subprocess();
    ~Subprocess();
    int wait();
    int write(const std::string &data);
    int write(const char *data, size_t len);
    int read(char *buffer, size_t size, StandardFd fd=SUBP_STDOUT);
    int read(std::string &data, StandardFd fd=SUBP_STDOUT);
    void shutdown();
    int get_fd_stdin();
    int get_fd_stdout();
    int get_fd_stderr();

private:
    pid_t pid;
    int pipes[3][2]; // stdin/stdout/stderr, read/write,
    int init_pipes();
    void child_setup_pipes();
    void parent_close_pipes();
    std::string getlineBuffer; // data read from stdout but not yet consumed
    void close_pipes();

};

#endif // _subprocess_h
