# Simple TCP Multiplexer (Simux)

## Introduction

Simux makes several TCP/IP clients talk to a single server through a single channel (TCP/IP connection, etc.).

To achieve this:

- only one client at a time can talk to the server;
- the other clients are put on hold;
- the talking client must close its socket to indicate it has completed.

A typical usage is sending several HTTP requests through a single TLS tunnel.


## Architecture

The Simux architecture is as follows:

- it relies on a child process (not provided by Simux) in charge of connecting to the server, and communicates with the child via its stdin/stdout;
- it accepts one client at a time and forwards the data from the client to the child, and vice versa.


## Usage

	Usage: simux [-v] PORT COMMAND [ARGS ...]

	Arguments:
  		-v         Be verbose
  		PORT       Local listening TCP port
  		COMMAND    Command to launch for connecting to the server
  		ARGS       Arguments passed to COMMAND


## Example: HTTP requests through a single TLS tunnel

Start Simux:

    simux 8888 openssl s_client -quiet -connect example.org:443


Run HTTP clients:

    curl http://localhost:8888/hello.html
	curl http://localhost:8888/search?q=hello
	...


