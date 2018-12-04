Simple TCP Multiplexer

# Introduction

The Simple TCP Multiplexer forwards the requests of multiple clients through a single TCP channel to a server, and writes back the responses.
The Simple TCP Multiplexer accepts one client at a time. The clients must close the socket after it has completed.

The typical usage is for tunneling several HTTP requests through a single TLS tunnel.


# Example

Start Simple TCP Multiplexer:

    ./simple_tcp_mux 8888 openssl s_client -quiet -connect example.org:443


Run HTTP clients:

    curl http://localhost:8888/
	curl http://localhost:8888/
	...

