#pragma once
#include "http_base.h"

namespace tms {

    //---------------------------------------------------------------------------------------------
    // ugly http or https stream

    struct http_stream {
        using tcp_stream_type = beast::tcp_stream;
        optional<tcp_stream_type> tcp;
#ifndef ASIO_POOL_HTTPS_IGNORE
        using ssl_stream_type = beast::ssl_stream<tcp_stream_type>;
        optional<asio::ssl::context> ssl_context;
        optional<ssl_stream_type> ssl;
#endif

        inline tcp_stream_type* get() {
#ifndef ASIO_POOL_HTTPS_IGNORE
            if (ssl) {
                return &beast::get_lowest_layer(*ssl);
            }
#endif
            if (tcp) {
                return &*tcp;
            }
            return nullptr;
        }

#ifndef ASIO_POOL_HTTPS_IGNORE
        void configure(https_method _method) {
            ssl_context.emplace(_method);
            ssl_context->set_verify_mode(asio::ssl::verify_peer);
            ssl_context->set_default_verify_paths();
            // load_root_certificates(ssl_context);
        }
#endif

        void init(const asio_executor& ex) {
#ifndef ASIO_POOL_HTTPS_IGNORE
            if (ssl_context) {
                ssl.emplace(ex, *ssl_context);
                tcp.reset();
                return;
            }
            ssl.reset();
#endif
            tcp.emplace(ex);
        }
        bool valid() {
            if (auto stream = get()) {
                auto& socket = stream->socket();
                return socket.is_open(); // always is open
            }
            return false;
        }
        void expires_after(unsigned int secs) {
            if (auto stream = get()) {
                stream->expires_after(std::chrono::seconds(secs));
            }
        }
        void connect(const tcp_endpoints& endpoints, std::function<void(http_error err, tcp_endpoint endpoint)>&& handler) {
            if (auto stream = get()) {
                stream->async_connect(endpoints, std::move(handler));
                /*
                stream->async_connect(endpoints, [h = std::move(handler)](http_error err, tcp_endpoint endpoint) {
                    h(err, endpoint);
                });
                */
            }
        }
#ifndef ASIO_POOL_HTTPS_IGNORE
        void handshake(const std::string& hostname, std::function<void(http_error err)>&& handler) {
            if (ssl) {
                // error after handshake [asio.ssl:369098857] unregistered scheme (STORE routines)
                // ssl->set_verify_mode(asio::ssl::verify_peer);
                // ssl->set_verify_callback(asio::ssl::host_name_verification(hostname));
                if (!SSL_set_tlsext_host_name(ssl->native_handle(), hostname.c_str())) {
                    return handler(http_error{ static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category() });
                }
                ssl->async_handshake(ssl_stream_type::client, std::move(handler));
            }
        }
#endif
        void shutdown() {
            if (auto stream = get()) {
                stream->socket().shutdown(tcp::socket::shutdown_both);
            }
        }
        void reset() {
            tcp.reset();
#ifndef ASIO_POOL_HTTPS_IGNORE
            ssl.reset();
#endif
        }
    };
    
    //---------------------------------------------------------------------------------------------
    // ugly request with different body types

    class http_request {
    public:
        typedef std::function<void(http_error, size_t)> process_handler_type;
        virtual ~http_request() {}
        virtual const http_string get(http_string key) = 0;
        virtual void set(http_string key, const http_string &value) = 0;
        virtual void write(http_stream& stream, process_handler_type handler) = 0;
        virtual void read(http_stream& stream, process_handler_type handler) = 0;
        virtual void end(http_error err, http_stage stage) = 0;
    };

    typedef std::shared_ptr<http_request> http_request_ptr;

    typedef http::empty_body http_empty_body;
    typedef http::string_body http_string_body;
    //typedef http::vector_body<uint8_t> http_binary_body;
    typedef http::dynamic_body http_binary_body;

    typedef http::response<http_string_body> http_string_response;
    typedef http::response<http_binary_body> http_binary_response;

    template<typename request_body, typename response_body, typename handler_type>
    class http_request_t : public http_request {
    public:
        typedef http::request<request_body> request_type;
        typedef http::response<response_body> response_type;
        request_type request;
        response_type response;

        http_request_t(http_verb method, http_string target, handler_type h)
            : request(method, std::move(target), 11), handler(std::move(h))
        {}

        http_request_t(http_verb method, http_string target, std::string data, handler_type h)
            : request(method, std::move(target), 11), std::move(data), handler(std::move(h))
        {}

        virtual const http_string get(http_string key) {
            return response[std::move(key)];
        }

        virtual void set(http_string key, const http_string& value) {
            request.set(std::move(key), value);
        }

        // ugly write with ugly stream
        virtual void write(http_stream& stream, process_handler_type handler) {
            

#ifndef ASIO_POOL_HTTPS_IGNORE
            if (stream.ssl) {
                http::async_write(*stream.ssl, request, handler);
                return;
            }
#endif
            if (stream.tcp) {
                http::async_write(*stream.tcp, request, handler);
            }
        }

        // ugly read with ugly stream
        virtual void read(http_stream& stream, process_handler_type handler) {

#ifndef ASIO_POOL_HTTPS_IGNORE
            if (stream.ssl) {
                http::async_read(*stream.ssl, buffer, response, handler);
                return;
            }
#endif
            if (stream.tcp) {
                http::async_read(*stream.tcp, buffer, response, handler);
            }
        }

        virtual void end(http_error err, http_stage stage) {
            std::move(handler)(err, stage, std::forward<response_type>(response));
        }

    protected:
        beast::flat_buffer buffer;
        handler_type handler;
    };

    template<typename handler_type>
    class http_binary_get : public http_request_t<http_empty_body, http_binary_body, handler_type> {
    public:
        http_binary_get(http_string target, handler_type h)
            : http_request_t<http_empty_body, http_binary_body, handler_type>(http_verb::get, std::move(target), std::move(h))
        {
        }
    };

    template<typename handler_type>
    class http_json_get: public http_request_t<http_empty_body, http_string_body, handler_type> {
    public:
        http_json_get(http_string target, handler_type h)
           : http_request_t<http_empty_body, http_string_body, handler_type>(http_verb::get, std::move(target), std::move(h))
        {
            this->request.set(http::field::accept, "application/json");
        }    
    };

    template<typename handler_type>
    class http_json_post : public http_request_t<http_string_body, http_string_body, handler_type> {
    public:
        http_json_post(http_string target, std::string data, handler_type h)
            : http_request_t<http_string_body, http_string_body, handler_type>(http_verb::post, std::move(target), std::move(data), std::move(h))
        {
            this->request.set(http::field::content_type, "application/json");
            this->request.set(http::field::accept, "application/json");
        }
    };

    //---------------------------------------------------------------------------------------------
}
