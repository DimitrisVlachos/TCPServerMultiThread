#ifndef __tcp_server_hpp__
#define __tcp_server_hpp__


/*
    Author : Dimitris Vlachos
    Email : DimitrisV22@gmail.com
    Git : https://github.com/DimitrisVlachos
*/

#include <stdio.h>
#include <arpa/inet.h>    
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>  
#include <stdint.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <chrono>
#include <future>
#include <errno.h>
#include <unistd.h>    
#include <string.h>

#define trace printf
#define trace_error perror

struct read_buffer_t {
	read_buffer_t(uint8_t* in_data,const size_t in_size) : data(in_data),size(in_size) {  }
	read_buffer_t() : data(nullptr) {  }
	~read_buffer_t() {   }
	uint8_t* data;
	size_t size;
};

struct write_buffer_t {
	write_buffer_t(uint8_t* in_data,const size_t in_size) : data(in_data),size(in_size) {}
	write_buffer_t() : data(nullptr) {}
	~write_buffer_t() {delete[] data;}
	uint8_t* data;
	size_t size;
};

struct client_ctx_t {
	client_ctx_t() : buffer(nullptr) { }
	~client_ctx_t() { delete[] buffer; clear_rd_buffer(); clear_wr_buffer(); }
	int32_t fd;
	int32_t port;
	int32_t read_len;
	uint8_t* buffer;
	std::atomic<bool> running;
	std::string ip;
	fd_set set;
	std::thread thread;
	std::mutex m_rd_mtx,m_wr_mtx;

	inline void lock_rd_buffer() {
		while (!m_rd_mtx.try_lock()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
	}

	inline void unlock_rd_buffer() {
		m_rd_mtx.unlock();
	}

	inline void lock_wr_buffer() {
		while (!m_wr_mtx.try_lock()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
	}

	inline void unlock_wr_buffer() {
		m_wr_mtx.unlock();
	}

	//XXX WR BUFFER Buffer must be locked in order to use these functions
	inline void push_wr_buffer(void* data,const size_t size) {
		m_wr_q.push( new write_buffer_t( (uint8_t*)data,size) );
	}

	inline write_buffer_t* pop_wr_buffer() {
		if (m_wr_q.empty())
			return nullptr;

		write_buffer_t* tmp = m_wr_q.front();
		m_wr_q.pop();
		return tmp;
	}

	inline void clear_wr_buffer() {
		while (!m_wr_q.empty()) {
			delete m_wr_q.front();
			m_wr_q.pop();
		}
	}

	inline const bool wr_buf_has_pending_data() const { 
		return !m_wr_q.empty();
	}

	//XXX RD BUFFER Buffer must be locked in order to use these functions
	inline void push_rd_buffer(void* data,const size_t size) {
		m_rd_q.push( new   read_buffer_t((uint8_t*)data,size)  );
	}

	inline read_buffer_t* pop_rd_buffer() {
		if (m_rd_q.empty())
			return nullptr;

		read_buffer_t* tmp = m_rd_q.front();
		m_rd_q.pop();
		return tmp; //make sure to cleanup!!
	}

	inline void clear_rd_buffer() {
		while (!m_rd_q.empty()) {
			delete m_rd_q.front();
			m_rd_q.pop();
		}
	}

	inline const bool rd_buf_has_pending_data() const {
		return !m_rd_q.empty();
	}

	inline std::queue<read_buffer_t*>& get_rd_queue() {
		return m_rd_q;
	}

	private:
	std::queue<read_buffer_t*> m_rd_q;
	std::queue<write_buffer_t*> m_wr_q;
};

class tcp_server_c {
	private:
	std::vector<client_ctx_t*> m_clients;
	std::atomic<bool> m_running;
	fd_set m_socket_set;
	std::thread m_server_thread;
	bool m_init_done;
	bool m_use_rw_buffers;
	uint32_t m_thread_delay;
	int32_t m_socket;
	int32_t m_port;
	uint32_t m_max_socket_index;
	uint32_t m_read_len;
 	

	void base_thread() {
		const bool use_rw_buf = m_use_rw_buffers;
		const uint32_t thread_delay = m_thread_delay;
		struct sockaddr_in address;
		int32_t addrlen = sizeof(address);
		int32_t new_socket;
		int32_t rd_res;
		struct timeval timeout_select = {0,0};

		while (m_running.load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(thread_delay));

			timeout_select.tv_sec = 2;
			FD_ZERO(&m_socket_set); 
			FD_SET(m_socket, &m_socket_set);
 
			//TODO Do cleanup pass after X time

			for (size_t i = 0;i < m_clients.size();++i) {		
				if (!m_clients[i]->running.load(std::memory_order_acquire)) {
					const size_t last = m_clients.size() - 1;
					client_ctx_t* tmp = m_clients[i];
					tmp->thread.detach();
					m_clients[i] = m_clients[last];
					m_clients.pop_back();
					delete tmp;
					i = 0;
				}
			}

		    const int32_t active = select( m_socket + 1 , &m_socket_set , NULL , NULL , &timeout_select);
		
		    if ((active < 0) && (errno!=EINTR) )  {
		        trace("select");
		    }

		    if (FD_ISSET(m_socket, &m_socket_set)) {
		        if ((new_socket = accept(m_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
		            trace_error("accept");
		            exit(EXIT_FAILURE);
		        }

		        client_ctx_t* cl = new client_ctx_t();
				if (!cl) {
					trace_error("cl NEW\n");
					exit(EXIT_FAILURE);
				}
				cl->port = ntohs(address.sin_port);
				cl->ip = std::string(inet_ntoa(address.sin_addr));
		        cl->fd = new_socket;
				cl->read_len = m_read_len;
				cl->buffer = new uint8_t[m_read_len];
				if (!cl->buffer) {
					trace_error("buffer NEW\n");
					exit(EXIT_FAILURE);
				}
				cl->running.store(true);
				
				FD_SET(cl->fd,&m_socket_set);
				m_clients.push_back(cl);

				
				cl->thread = std::thread(&tcp_server_c::connection_task,this,cl);
				this->on_connect(cl);
		    }
		}
	}

	void connection_task(client_ctx_t* cl) {
		const bool use_rw_buf = m_use_rw_buffers;
		const uint32_t thread_delay = m_thread_delay;
		struct sockaddr_in address;
		int32_t addrlen = sizeof(address);
		int32_t new_socket;
		int32_t rd_res;
		struct timeval timeout_select = {0,0};
		
		while (cl->running.load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(thread_delay));
			timeout_select.tv_sec = 2;
			FD_ZERO(&cl->set); 
			FD_SET(cl->fd, &cl->set);

		    const int32_t active = select( cl->fd + 1 , &cl->set , NULL , NULL , &timeout_select);
		
		    if ((active < 0) && (errno!=EINTR))  {
		        trace("select error");
		    }

			if (FD_ISSET(cl->fd, &cl->set)) {
				if (!FD_ISSET(cl->fd ,&cl->set)) 
					continue;
				rd_res = read(cl->fd ,cl->buffer,cl->read_len);
				if (rd_res <= 0) {
					getpeername(cl->fd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
					this->on_disconnect(cl);
					close( cl->fd );
					cl->running.store(false,std::memory_order_release);
					break;
				}
				if (use_rw_buf) {
					cl->lock_rd_buffer();
					cl->push_rd_buffer((void*)cl->buffer,(size_t)rd_res);
					cl->buffer = new uint8_t[cl->read_len];
					if (!cl->buffer) {
						trace_error("buffer new\n");	
						exit(EXIT_FAILURE);
					}
					cl->unlock_rd_buffer();
				} else { //block
					this->on_recv_data(cl, cl->buffer,(size_t)rd_res);
				}
			}

			if (use_rw_buf) { 
				cl->lock_wr_buffer(); //TODO merge to single ?
				write_buffer_t* tmp;
				while ((tmp = cl->pop_wr_buffer()) != nullptr) {
					send(cl->fd,tmp->data,tmp->size,0);
					delete tmp; 
				} 
				cl->unlock_wr_buffer();
			}
		}
	}

	public:
	tcp_server_c() : m_init_done(false),m_thread_delay(100),m_max_socket_index(0),m_use_rw_buffers(true) {
		m_running.store(false);
	}

	~tcp_server_c() {	
		this->stop_thread();
	}

	//Note this will never get called if : m_use_rw_buffers is set
	virtual void on_recv_data(client_ctx_t* ctx,const uint8_t* data,const size_t size) {
		trace("on recv %s:%d %lu\n",ctx->ip.c_str(),ctx->port,size);
	}

	virtual void on_connect(client_ctx_t* ctx) {
		trace("on connect %s:%d\n",ctx->ip.c_str(),ctx->port);
	}

	virtual void on_disconnect(client_ctx_t* ctx) {
		trace("on diconnect %s:%d\n",ctx->ip.c_str(),ctx->port);
	}

	inline std::vector<client_ctx_t*>& get_clients() {
		return m_clients;
	}

	//Make sure to delete dest
	bool recv_data(client_ctx_t* ctx,uint8_t*& dest_ref_ptr,uint32_t& read_data) {
		if (!m_use_rw_buffers) //Note : not atomic οr in critical  section due to the fact that it gets set once at startup then threads just read its memory :)
			return false;

		ctx->lock_rd_buffer();
		read_buffer_t* buf = ctx->pop_rd_buffer();
		if (nullptr == buf) {
			ctx->unlock_rd_buffer();
			dest_ref_ptr = nullptr;
			return false;
		}
		dest_ref_ptr = buf->data;
		read_data = buf->size;
		ctx->unlock_rd_buffer();

		return true;
	}

	bool send_data(client_ctx_t* ctx,const void* src,const size_t sz) {
		
		if (m_use_rw_buffers) {
			uint8_t* tmp = new uint8_t[sz];
			if (!tmp)
				return false;

			memcpy(tmp,src,sz);

			ctx->lock_wr_buffer();
			ctx->push_wr_buffer(tmp,sz);
			ctx->unlock_wr_buffer();
			return true;
		}

		return send(ctx->fd,src,sz,0) == sz;
	}

	bool open(const int32_t port,const uint32_t read_len,const uint32_t max_backlog_requests = 10) {
		int32_t opt = 1;
		struct sockaddr_in address;

		stop_thread();

		m_read_len = read_len;

		if( (m_socket = socket(AF_INET , SOCK_STREAM , 0)) == 0)  {
		    trace_error("socket");
		    return false;
		}
  
		if( setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 ) {
		    trace_error("setsockopt");
		    return false;
		}

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons( port );
		  
		if (bind(m_socket, (struct sockaddr *)&address, sizeof(address)) <  0)  {
		    trace_error("bind");
		    exit(EXIT_FAILURE);
		}

		FD_ZERO(&m_socket_set);
 
		if (listen(m_socket, max_backlog_requests) < 0) {
		    trace_error("listen");
		    exit(EXIT_FAILURE);
		}

        FD_SET(m_socket, &m_socket_set);
		m_max_socket_index = m_socket;
		m_init_done = true;

		return true;
	}

	bool start_thread(const bool use_rw_buffers = true,const uint32_t timeout_ms = 2000,const uint32_t thread_delay_ms = 100) {
		if ((!m_init_done)  || is_running())
			return true;

		m_use_rw_buffers = use_rw_buffers;
		m_thread_delay = thread_delay_ms;
		m_running.store(true,std::memory_order_release);
		m_server_thread = std::thread(&tcp_server_c::base_thread,this);
		return true;
	}

	bool stop_thread() {
		if (!m_init_done) 
			return false;
		else if (!is_running())
			return true;

		m_running.store(false,std::memory_order_release);
		for (size_t i = 0;i < m_clients.size();++i) {	
			m_clients[i]->running.store(false,std::memory_order_release);

			if (m_clients[i]->thread.joinable())
				m_clients[i]->thread.join();
		}
		m_clients.clear();

		if (m_server_thread.joinable())
			m_server_thread.join();

		m_init_done = false;
		return true;
	}

	const inline bool is_running()  {
		return m_running.load(std::memory_order_acquire);
	}
};
 

#endif