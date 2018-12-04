#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "subprocess.h"
#include "logging.h"

#define BUF_SIZ 4096

/** Initialize the pipes for communication between parent and child
 *
 *  On error, -1 is returned. Some pipes may have been initialized and others not.
 *
 */
int Subprocess::init_pipes()
{
    int err;

    err = pipe2(pipes[SUBP_STDIN], O_CLOEXEC);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stdin: %s", strerror(errno));
        return -1;
    }

    err = pipe2(pipes[SUBP_STDOUT], O_CLOEXEC);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stdout: %s", strerror(errno));
        return -1;
    }

    err = pipe2(pipes[SUBP_STDERR], O_CLOEXEC);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stderr: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/** Move the file descriptor to a value greater than 2
 *
 *  When duplicated, the original file descriptor and
 *  other created file descriptors are not closed.
 *  The caller is suppose to take care of closing them.
 *
 *  This function is intended to be called in the
 *  child between the fork and the exec.
 */
static void child_move_fd_above_2(int &fd)
{
    if (fd <= 2) {
        // need to be duplicated to a value >= 3
        int newFd = dup(fd);
        if (-1 == newFd) {
            // This is an unpleasant case.
            // Logging cannot be done, as we are between fork and exec
            return;
        }
        child_move_fd_above_2(newFd);
        fd = newFd;
    }
}

void Subprocess::child_setup_pipes()
{
    // We are between fork and exec. We need to setup the
    // standard files descriptors 0, 1, 2 of the child.

    // the pipes have been open with the flag O_CLOEXEC, therefore
    // we do not care to close them explicitely (the exec will).
    child_move_fd_above_2(pipes[SUBP_STDIN][SUBP_READ]);
    child_move_fd_above_2(pipes[SUBP_STDOUT][SUBP_WRITE]);
    child_move_fd_above_2(pipes[SUBP_STDERR][SUBP_WRITE]);

    // The close-on-exec flag for the duplicate descriptor is off
    dup2(pipes[SUBP_STDIN][SUBP_READ], STDIN_FILENO);
    dup2(pipes[SUBP_STDOUT][SUBP_WRITE], STDOUT_FILENO);
    dup2(pipes[SUBP_STDERR][SUBP_WRITE], STDERR_FILENO);
}

static void close_pipe(int &fd)
{
    int err = close(fd);
    if (err) {
        LOG_ERROR("close_pipe: cannot close %d: %s", fd, strerror(errno));
        return;
    }
    fd = -1;
}

void Subprocess::parent_close_pipes()
{
    close_pipe(pipes[SUBP_STDIN][SUBP_READ]);
    close_pipe(pipes[SUBP_STDOUT][SUBP_WRITE]);
    close_pipe(pipes[SUBP_STDERR][SUBP_WRITE]);
}


/** Close stdin, stdout, sterr
 */
void Subprocess::close_pipes()
{
    int i, j;
    for (i=0; i<3; i++) for (j=0; j<2; j++) {
        if (pipes[i][j] != -1) close_pipe(pipes[i][j]);
    }
}

/** Request the end of the child process and wait
 *
 *  At the moment, the termination of the child process
 *  relies on the closing of the file descriptors.
 */
void Subprocess::shutdown()
{
    close_pipes();
    int err = wait();
    if (err) {
        LOG_ERROR("Subprocess::shutdown: err=%d", err);
    }
}


Subprocess::Subprocess()
{
    int i, j;
    for (i=0; i<3; i++) for (j=0; j<2; j++) pipes[i][j] = -1;
}

Subprocess::~Subprocess()
{
    // close remaining open pipes
    close_pipes();
}

/** Launch a subprocess
 *
 * @param args
 * @param envp
 * @param dir
 */
Subprocess *Subprocess::launch(char *const argv[], char *const envp[], const char *dir)
{
    std::string debugArgv;
    char *const *ptr;

    ptr = argv;
    while (*ptr) {
        if (ptr != argv) debugArgv += " ";
        debugArgv += *ptr;
        ptr++;
    }

    std::string debugEnvp;
    if (envp) {
        ptr = envp;
        while (*ptr) {
            if (ptr != envp) debugEnvp += " ";
            debugEnvp += *ptr;
            ptr++;
        }
    }

    const char *dirStr = "."; // used for debug
    if (dir) dirStr = dir;
    LOG_INFO("Subprocess::launch: %s (env=%s, dir=%s)", debugArgv.c_str(), debugEnvp.c_str(), dirStr);

    Subprocess *handler = new Subprocess();

    int err = handler->init_pipes();
    if (err) {
        delete handler;
        return 0;
    }

    handler->pid = fork();
    if (handler->pid < 0) {
        LOG_ERROR("Cannot fork");
        delete handler;
        return 0;
    }

    if (handler->pid == 0) {
        // in child
        if (dir) {
            if (chdir(dir) != 0) {
                _exit(72);
            }
        }

        // setup redirection
        handler->child_setup_pipes();

        execvpe(argv[0], argv, envp);
        _exit(72);
    }

    // in parent
    handler->parent_close_pipes();

    return handler;
}


int Subprocess::wait()
{
    int status;
    int rc;
    rc = waitpid(pid, &status, 0);
    if (rc < 0) {
        LOG_ERROR("waitpid error: %s", strerror(errno));
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    LOG_ERROR("waitpid: unexpected code path");
    return -1;
}

/** Write to the standard input of the subprocess
 */
int Subprocess::write(const std::string &data)
{
    return write(data.data(), data.size());
}

int Subprocess::write(const char *data, size_t len)
{
    size_t remaining = len;

    while (remaining) {
        ssize_t n = ::write(pipes[SUBP_STDIN][SUBP_WRITE], data, remaining);
        if (n < 0) {
            LOG_ERROR("Subprocess::write() error: %s", strerror(errno));
            return -1;
        }
        remaining -= n;
        data += n;
    }
    return 0;
}


/** Return the file descriptor for writing to the stdin of the child process
 */
int Subprocess::get_fd_stdin()
{
    return pipes[SUBP_STDIN][SUBP_WRITE];
}

/** Return the file descriptor for reading the stdout of the child process
 */
int Subprocess::get_fd_stdout()
{
    return pipes[SUBP_STDOUT][SUBP_READ];
}


/** Return the file descriptor for reading the stdout of the child process
 */
int Subprocess::get_fd_stderr()
{
    return pipes[SUBP_STDERR][SUBP_READ];
}
