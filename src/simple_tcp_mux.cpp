
/*
 */
 
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/tcp.h>


#include <poll.h>

#include <signal.h>

#include "logging.h"
#include "subprocess.h"

const char *usage_string = \
        "Usage: simple_tcp_mux [-v] <local-port> command ...\n"
        "\n"
        "Options:\n"
        "  -v     be verbose\n"
        "\n"
        "Example:\n"
        "  simple_tcp_mux 8888 openssl s_client -quiet -connect example.com:443\n"
        ;

void usage()
{
    fprintf(stderr, "%s", usage_string);
	exit(1);
}


static std::string hexlify(const unsigned char *data, const size_t len)
{
    const char hex_table[] = { '0', '1', '2', '3',
                               '4', '5', '6', '7',
                               '8', '9', 'a', 'b',
                               'c', 'd', 'e', 'f' };
    std::string hex_result;
    size_t i;

    for (i=0; i<len; i++) {
        unsigned char c = data[i];
        hex_result += hex_table[c >> 4];
        hex_result += hex_table[c & 0x0f];
    }
    return hex_result;
}


static void cleanup_child(Subprocess *&subp, int &fd_stdout, int &fd_stderr)
{
    delete subp;
    subp = 0;
    fd_stdout = -1;
    fd_stderr = -1;
}


static void cleanup_client(int &fd, bool server_side_disconnect=false)
{
    if (fd >= 0) {
        if (server_side_disconnect) LOG_INFO("Disconnect client");
        close(fd);
    }
    fd = -1;
}


Subprocess *start_tunnel_process(int argc, const char * const *args, int &fd_stdout, int &fd_stderr)
{
    Argv argv;
    Subprocess *subp;

    if (argc <= 0) return 0; // this is a usage error

    // Prepare the arguments
    argv.set(argc, args);

    subp = Subprocess::launch(argv.getv(), 0, 0);
    if (!subp) {
        LOG_ERROR("start_tunnel_process failed to launch: %s", argv.to_string().c_str());
        return 0; // error
    }

    fd_stdout = subp->get_fd_stdout();
    fd_stderr = subp->get_fd_stderr();

    return subp; // success
}



int handle_requests(int listening_sock, int child_argc, const char * const *child_argv)
{
    const int BUF_SIZ = 1024;
    unsigned char buffer[BUF_SIZ];
    struct pollfd *listener;
    Subprocess *subp = 0;
    struct pollfd *child_stdout;
    struct pollfd *child_stderr;
    struct pollfd *client;
    const int NFDS = 5;
    struct pollfd fds[NFDS];
    int err = 0;

    // setup the pollfd table

    int index = 0;
    fds[index].fd = listening_sock;
    fds[index].events = POLLIN;
    listener = &fds[index];

    index = 1;
    fds[index].fd = -1; // reserved for child stdout
    fds[index].events = POLLIN|POLLHUP|POLLERR;
    child_stdout = &fds[index];

    index = 2;
    fds[index].fd = -1; // reserved for child stderr
    fds[index].events = POLLIN;
    child_stderr = &fds[index];

    index = 3;
    fds[index].fd = -1; // reserved for connected cilent
    fds[index].events = POLLIN|POLLHUP|POLLERR;
    client = &fds[index];

	while (1) {

        err = poll(fds, NFDS, 10000); // arbitrary 10 second timeout

        if (err == -1) {
            LOG_ERROR("poll error: %s", strerror(errno));
            err = -1;
            break;
        }

        if (err == 0) {
            // Timeout occurred
            LOG_VERBOSE("poll timeout");
            continue;
		}

        if (listener->revents & POLLIN) {

            // a new client is connecting
            // accept pending client connection request
            int client_sock = accept(listener->fd, NULL, NULL);

            if (client_sock < 0) {
                LOG_ERROR("accept error: %s", strerror(errno));
                continue;
            }

            LOG_INFO("New client accepted");

            // remove the listening sock from the event detection mechanism,
            // while this client is connected
            listener->fd = -1;

            client->fd = client_sock; // insert in the event detection mechanism

            // If no child process is running, then start it
            if (!subp) {
                subp = start_tunnel_process(child_argc, child_argv, child_stdout->fd, child_stderr->fd);
                if (!subp) {
                    LOG_ERROR("Cannot start child process");
                    err = -1;
                    break; // exit
                }
            }
        }

        if ( (child_stdout->revents & POLLHUP) || (child_stdout->revents & POLLERR)) {
            LOG_INFO("Child process terminated (%s)", (child_stdout->revents&POLLHUP)?"HUP":"ERR");
            cleanup_child(subp, child_stdout->fd, child_stderr->fd);
            cleanup_client(client->fd, true);
            listener->fd = listening_sock; // start tracking new clients again
            continue;
        }

        if (child_stdout->revents & POLLIN) {

            // child is sending data back to the client

            int n = read(child_stdout->fd, buffer, BUF_SIZ);
			if (n < 0) {
                LOG_ERROR("read from child stdout error: %s", strerror(errno));
                cleanup_child(subp, child_stdout->fd, child_stderr->fd);
                // disconnect the client, if any
                cleanup_client(client->fd);
                listener->fd = listening_sock; // start tracking new clients again
                continue;
            }

			if (n == 0) {
                LOG_INFO("Child process terminated (stdout closed?)");
                cleanup_child(subp, child_stdout->fd, child_stderr->fd);
                // disconnect the client, if any
                cleanup_client(client->fd);
                listener->fd = listening_sock; // start tracking new clients again
                continue;
            }

            if (client->fd >= 0) {
                // Send to the client
                LOG_VERBOSE("< %s", hexlify(buffer, n).c_str());

                n = write(client->fd, buffer, n);
                if (n < 0) {
                    LOG_ERROR("Cannot write to client (disconnected?): %s", strerror(errno));
                    // close the client socket
                    close(client->fd);
                    client->fd = -1;
                }
            } else {
                // no connected client
                LOG_INFO("Got data from child, but client disconnected. Ignore");
                // TODO ? stop the child, to prevent discrepancies?
            }

        }

        if (child_stderr->revents & POLLIN) {

            // we assume that the child writes readable text to its stderr.
            int n = read(child_stderr->fd, buffer, BUF_SIZ-1); // -1 to let space for a null character (see below)
			if (n < 0) {
                LOG_ERROR("read from child stderr error: %s", strerror(errno));
                cleanup_child(subp, child_stdout->fd, child_stderr->fd);
                // disconnect the client, if any
                cleanup_client(client->fd);
                listener->fd = listening_sock; // start tracking new clients again
                continue;
            }
			if (n == 0) {
                LOG_INFO("Child process terminated (stderr closed?)");
                cleanup_child(subp, child_stdout->fd, child_stderr->fd);
                // disconnect the client, if any
                cleanup_client(client->fd);
                listener->fd = listening_sock; // start tracking new clients again
                continue;
            }

            buffer[n] = '\0'; // terminating null char to insure sane logging
            LOG_INFO("Child stderr: %s", buffer);
        }

        if ( (client->revents & POLLHUP) || (client->revents & POLLERR)) {
            LOG_INFO("Client disconnected: event=%d", client->revents);
            cleanup_client(client->fd);
            listener->fd = listening_sock; // start tracking new clients again
            continue;
        }

        if (client->revents & POLLIN) {
            // the client is sending data

            int n = read(client->fd, buffer, BUF_SIZ);
            if (n < 0) {
                LOG_ERROR("read from client error: %s", strerror(errno));
                // disconnect the client
                cleanup_client(client->fd);
                listener->fd = listening_sock; // start tracking new clients again
                continue;
            }

            if (n == 0) {
                // client terminated
                // disconnect the client
                LOG_INFO("Client disconnected");
                cleanup_client(client->fd);
                listener->fd = listening_sock; // start tracking new clients again
                continue;
            }

            // send to the child process
            LOG_VERBOSE("> %s", hexlify(buffer, n).c_str());

            if (!subp) {
                // the child process has terminated
                LOG_ERROR("Cannot send to child (terminated)");
                cleanup_client(client->fd);
                listener->fd = listening_sock; // start tracking new clients again
                continue;
            }

            n = write(subp->get_fd_stdin(), buffer, n);
            if (n < 0) {
                LOG_ERROR("Cannot write to the child process (terminated?): %s", strerror(errno));
                cleanup_child(subp, child_stdout->fd, child_stderr->fd);
                // disconnect the client, if any
                listener->fd = listening_sock; // start tracking new clients again
                cleanup_client(client->fd);
                continue;
            }
        }
	}
 
    cleanup_child(subp, child_stdout->fd, child_stderr->fd);
    cleanup_client(client->fd);

    LOG_INFO("handle_requests() completed");

    return err;
}


int start_tcp_mux(int port, int child_argc, const char * const *child_argv)
{
    struct sockaddr_in sock_addr;
    int listening_sock;
    int opt_true = 1;
    int err;

    listening_sock = socket(PF_INET, SOCK_STREAM, 0);

    if (listening_sock < 0) {
        LOG_ERROR("socket error: %s", strerror(errno));
        return -1;
    }

    err = setsockopt(listening_sock, SOL_SOCKET, SO_REUSEADDR, &opt_true, sizeof opt_true);
    if (err < 0) {
        LOG_ERROR("setsockopt error: %s", strerror(errno));
        close(listening_sock);
        return -1;
    }

    memset(&sock_addr, 0, sizeof sock_addr);

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = INADDR_ANY;

    err = bind(listening_sock,(struct sockaddr *)&sock_addr, sizeof sock_addr);
    if (err < 0) {
        LOG_ERROR("bind error: %s", strerror(errno));
        close(listening_sock);
        return -1;
    }

    err = listen(listening_sock, 20);
    if (err < 0) {
        LOG_ERROR("listen error: %s", strerror(errno));
        close(listening_sock);
        return -1;
    }

    LOG_INFO("Listening on %d", port);

    err = handle_requests(listening_sock, child_argc, child_argv);

    close(listening_sock);

    return err;
}

int main(int argc, char **argv)
{
    if (argc < 2) usage();
    argc--;
    argv++; // skip the program name
    if (0 == strcmp(argv[0],"-v")) {
        log_set_verbose(1);
        argc--;
        argv++; // skip the -v
    }
    if (argc < 2) usage();

    int port = atoi(argv[0]);

    argc--;
    argv++; // skip the port argument

    start_tcp_mux(port, argc, argv);

    return 0;
}
