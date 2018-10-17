/*
    Author : Dimitris Vlachos
    Email : DimitrisV22@gmail.com
    Git : https://github.com/DimitrisVlachos
*/

#include "tcp_server.hpp"
#include <iostream>

class non_buffered_tcp_server_c : public tcp_server_c {
	private:
	public:
	non_buffered_tcp_server_c() {}
	~non_buffered_tcp_server_c() {}

	void on_recv_data(client_ctx_t* ctx,const uint8_t* data,const size_t size)   {
		trace("!on recv %s:%d %lu\n",ctx->ip.c_str(),ctx->port,size);
		const std::string tmp = ctx->ip + " requested : " + std::to_string(size) + "\n";

		this->send_data(ctx,(const void*)&tmp[0],tmp.length());
	}

	void on_connect(client_ctx_t* ctx)   {
		trace("!on connect %s:%d\n",ctx->ip.c_str(),ctx->port);
		const std::string tmp = "Hello";

		this->send_data(ctx,(const void*)&tmp[0],tmp.length());
	}

	void on_disconnect(client_ctx_t* ctx)   {
		trace("!on diconnect %s:%d\n",ctx->ip.c_str(),ctx->port);
	}
};

int main() {
	const bool non_blocking = false;

	tcp_server_c* srv = new non_buffered_tcp_server_c();

	srv->open(7770,1024);
	srv->start_thread(non_blocking);
	while (srv->is_running()) {
	}
	delete srv;
    return 0;
}

