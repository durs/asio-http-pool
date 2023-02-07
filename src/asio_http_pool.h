#pragma once
#include <list>
#include <map>
#include <deque>
#include <mutex>
#include <string>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/http.hpp>

#ifndef ASIO_POOL_HTTPS_IGNORE
#include <boost/asio/ssl.hpp>
#include <boost/beast/ssl.hpp>
#endif

namespace tms {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

using tcp = asio::ip::tcp;
using tcp_resolver = tcp::resolver;
using tcp_endpoints = tcp_resolver::results_type;
using tcp_endpoint = tcp_endpoints::endpoint_type;
using asio_executor = asio::any_io_executor;
using http_error = beast::error_code;
using http_string = beast::string_view;
using http_verb = http::verb;
using system_clock = std::chrono::system_clock;

#ifndef ASIO_POOL_HTTPS_IGNORE
using https_method = asio::ssl::context::method;
#else
using https_method = uint16_t;
#endif

static const int http_connect_tmout = 30;
static const int http_write_tmout = 30;
static const int http_read_tmout = 60;
static const int http_keep_tmout = 60;
static const int http_stats_tmout = 30;

enum http_stage {
    http_stage_none = 0,
    http_stage_resolve = 1,
    http_stage_connect = 2,
    http_stage_handshake = 3,
    http_stage_write = 4,
    http_stage_read = 5,
    http_stage_complete = 6
};

//---------------------------------------------------------------------------------------------
// ugly http or https stream

struct http_stream {
    using tcp_stream_type = beast::tcp_stream;
    std::optional<tcp_stream_type> tcp;
#ifndef ASIO_POOL_HTTPS_IGNORE
    using ssl_stream_type = beast::ssl_stream<tcp_stream_type>;
    std::optional<asio::ssl::context> ssl_context;
    std::optional<ssl_stream_type> ssl;
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
    void connect(const tcp_endpoints& endpoints, std::function<void(http_error err, tcp_endpoint endpoint)> &&handler) {
        if (auto stream = get()) {
            stream->async_connect(endpoints, std::move(handler));
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
    virtual void write(http_stream& stream, process_handler_type handler) {
        // ugly write with ugly stream

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
    virtual void read(http_stream& stream, process_handler_type handler) {
        // ugly read with ugly stream

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

class async_timer {
public:
    typedef std::function<void()> handler_type;
    using time_duration = boost::posix_time::time_duration;
    using seconds = boost::posix_time::seconds;
    using milliseconds = boost::posix_time::milliseconds;

    explicit async_timer(const asio::any_io_executor& ex)
        : timer(ex)
    {}
    void cancel() {
        timer.cancel();
    }
    bool wait(time_duration d, handler_type h) {
        duration = d;
        handler = std::move(h);
        return wait_next();
    }
    bool wait_next() {
        if (!handler) return false;
        timer.expires_from_now(duration);
        timer.async_wait([h = handler](boost::system::error_code err) {
            if (!err) h();
        });
        return true;
    }
private:
    asio::deadline_timer timer;
    time_duration duration;
    handler_type handler;
};

//---------------------------------------------------------------------------------------------

struct http_client_stats {
    int state = 0;
    size_t queue_size = 0;
    size_t error_count = 0;
    size_t total_requests = 0;
    size_t bytes_written = 0;
    size_t bytes_readed = 0;
    double total_seconds = 0;
};

class http_client :
    public std::enable_shared_from_this<http_client>
{
public:
    explicit http_client(const asio_executor& ex, std::string_view _host, std::string_view _port)
        : executor(ex), resolver(ex), timer(ex), host(_host), port(_port)
    {}

#ifndef ASIO_POOL_HTTPS_IGNORE
    explicit http_client(const asio_executor& ex, std::string_view _host, std::string_view _port, https_method _method)
        : executor(ex), resolver(ex), timer(ex), host(_host), port(_port)
    {
        stream.configure(_method);
        hostname = host;
        if (!port.empty()) {
            hostname += ":";
            hostname += port;
        }
    }
#endif

    template<typename Request>
    inline void enqueue(Request*&& req) {
        enqueue(http_request_ptr(std::move(req)));
    }

    inline void enqueue(http_request_ptr req) {
        asio::post(executor, std::bind(&http_client::append, shared_from_this(), std::move(req)));
        std::lock_guard lock(mutex);
        stats.queue_size++;
    }

    inline size_t queue_size() {
        // std::lock_guard lock(mutex);
        return stats.queue_size;
    }

    inline http_client_stats get_stats(bool reset) {
        std::mutex mutex;
        http_client_stats result = stats;
        if (reset) {
            stats.error_count = 0;
            stats.total_requests = 0;
            stats.bytes_written = 0;
            stats.bytes_readed = 0;
            stats.total_seconds = 0;
        }
        return result;
    }

protected:
    system_clock::time_point started = system_clock::now();
    asio_executor executor;
    tcp_resolver resolver;
    async_timer timer;
    http_stream stream;
    std::string host, port;
    std::deque<http_request_ptr> requests;
    std::mutex mutex;
    http_client_stats stats;
    int trycnt = 0;

#ifndef ASIO_POOL_HTTPS_IGNORE
    std::string hostname;
#endif
    
    bool check_result(http_error err, http_stage stage, size_t bytes = 0) {
        auto complete = stage == http_stage_complete || err;

        // on error reset stream
        if (err) {
            stream.reset();

            // may be stream closed, reconnect and try again
            if (trycnt == 0 && (stage == http_stage_write || stage == http_stage_read)) {
                trycnt++;
                asio::post(executor, std::bind(&http_client::next, shared_from_this()));
                complete = false;
            }
        }

        // complete current request
        if (complete) {
            trycnt = 0;
            set_complete(err, stage);
        }

        // update stats
        std::lock_guard lock(mutex);
        if (err) {
            stats.state = 0;
            stats.error_count++;
        }
        else {
            stats.state = 1;
        }
        if (bytes > 0) {
            ((stage == http_stage_write) ? stats.bytes_written : stats.bytes_readed) += bytes;
        }
        if (complete) {
            auto tmout = std::chrono::duration_cast<std::chrono::seconds>(system_clock::now() - started).count();
            stats.total_seconds += tmout * 1000;
            stats.total_requests++;
            stats.queue_size = requests.size();
        }
        return !err.failed();
    }

    void set_complete(http_error err, http_stage stage) {
        auto cnt = requests.size();
        if (cnt > 0) {
            auto req = requests.front();
            requests.pop_front();
            if (cnt > 1) {
                asio::post(executor, std::bind(&http_client::next, shared_from_this()));
            }
            else if (!err) {
                keep_alive(req->get("keep-alive"));
            }
            req->end(err, stage);
        }
    }

    void keep_alive(const http_string value) {
        int timeout = 0;
        if (!value.empty()) {
            size_t pos = value.find("timeout=");
            if (pos != decltype(value)::npos) {
                timeout = atoi(&value[pos + 8]);
            }
        }
        if (timeout <= 0) timeout = http_keep_tmout;
        keep_alive(timeout);
    }

    void keep_alive(int timeout = 0) {
        if (timeout > 0) {
            auto self = shared_from_this();
            timer.wait(async_timer::seconds(timeout), [self]() {
                self->shutdown();
            });
        }
        else {
            timer.cancel();
        }
    }

    void shutdown() {
        if (requests.empty() && stream.valid()) {
            stream.shutdown();
            stream.reset();
        }
    }

    void append(http_request_ptr req) {
        requests.push_back(std::move(req));
        if (requests.size() == 1) {
            process();
        }
    }

    void next() {
        if (!requests.empty()) {
            process();
        }
    }

    void process() {
        if (trycnt == 0) {
            started = system_clock::now();
        }
        keep_alive(0);
        if (stream.valid()) {
            return send();            
        }
        stream.init(executor);
        auto self = shared_from_this();
        resolver.async_resolve(host, port, beast::bind_front_handler(&http_client::on_resolve, self));
    }

    void send() {
        if (!requests.empty()) {
            auto self = shared_from_this();
            auto req = requests.front();
            if (trycnt == 0) {
                req->set("host", host);
                req->set("connection", "keep-alive");
                req->set("user-agent", BOOST_BEAST_VERSION_STRING);
            }
            stream.expires_after(http_write_tmout);
            req->write(stream, beast::bind_front_handler(&http_client::on_write, self));
        }
    }

    void on_resolve(http_error err, tcp_endpoints endpoints) {
        if (check_result(err, http_stage_resolve)) {
            auto self = shared_from_this();            
            stream.expires_after(http_connect_tmout);
            stream.connect(endpoints, beast::bind_front_handler(&http_client::on_connect, self));
        }
    }

    void on_connect(http_error err, tcp_endpoint endpoint) {
        if (check_result(err, http_stage_connect)) {
#ifndef ASIO_POOL_HTTPS_IGNORE
            if (stream.ssl) {
                auto self = shared_from_this();
                stream.handshake(hostname, beast::bind_front_handler(&http_client::on_handshake, self));
                return;
            }
#endif
            send();
        }
    }

    void on_handshake(http_error err) {
        if (check_result(err, http_stage_handshake)) {
            send();
        }
    }

    void on_write(http_error err, size_t transferred) {
        if (check_result(err, http_stage_write, transferred) && !requests.empty()) {
            auto self = shared_from_this();
            auto req = requests.front();
            stream.expires_after(http_read_tmout);
            req->read(stream, beast::bind_front_handler(&http_client::on_read, self));
        }
    }

    void on_read(http_error err, size_t transferred) {
        check_result(err, err ? http_stage_read : http_stage_complete, transferred);
    }
};

typedef std::shared_ptr<http_client> http_client_ptr;

//---------------------------------------------------------------------------------------------

struct http_pool_stats {
    size_t host_count = 0;
    size_t active_count = 0;
    size_t inactive_count = 0;
    size_t queue_size = 0;
    size_t bytes_written = 0;
    size_t bytes_readed = 0;
    double total_seconds = 0;
    double bandwidth = 0;
};

class http_client_pool :
    public std::enable_shared_from_this<http_client>
{
public:    
    explicit http_client_pool(const asio_executor& ex, size_t _maxcon_per_host = 2)
        : maxcon_per_host(_maxcon_per_host), executor(ex)
    {}

    template<typename response_body_type = http_binary_body, typename handler_type>
    inline void enqueue(std::string_view host, std::string_view port, std::string_view path, std::optional<https_method> https, handler_type handler) {
        using request_type = http_request_t<http_empty_body, response_body_type, handler_type>;
        auto req = std::make_shared<request_type>(http_verb::get, std::move(path), std::move(handler));
        enqueue(std::move(host), std::move(port), https, std::move(req));
    }

    template<typename response_body_type = http_binary_body, typename handler_type>
    inline void enqueue(std::string_view host, std::string_view port, std::string_view path, std::string data, std::optional<https_method> https, handler_type handler) {
        using request_type = http_request_t<http_string_body, response_body_type, handler_type>;
        auto req = std::make_shared<request_type>(http_verb::get, std::move(path), std::move(data), std::move(handler));
        enqueue(std::move(host), std::move(port), https, std::move(req));
    }

    template<typename request_type>
    inline void enqueue(std::string_view host, std::string_view port, std::optional<https_method> https, request_type*&& req) {
        enqueue(std::move(host), std::move(port), https, http_request_ptr(std::move(req)));
    }

    inline void enqueue(std::string_view host, std::string_view port, http_request_ptr req) {
        return enqueue(std::move(host), std::move(port), std::nullopt, std::move(req));
    }

    inline void enqueue(std::string_view host, std::string_view port, std::optional<https_method> https, http_request_ptr req) {
        std::string key(host);
        if (!port.empty()) {
            key += ":";
            key += port;
        }
#ifndef ASIO_POOL_HTTPS_IGNORE
        if (https) {
            key += ":ssl" + std::to_string(*https);
        }
#endif

        http_client_ptr client;
        {
            std::lock_guard lock(mutex);
            auto ptr = clients.find(key);
            if (ptr == clients.end()) {
#ifndef ASIO_POOL_HTTPS_IGNORE
                if (https) {
                    client = std::make_shared<http_client>(make_strand(executor), host, port, *https);
                }
                else
#endif
                {
                    client = std::make_shared<http_client>(make_strand(executor), host, port);
                }
                clients.emplace(key, clients_list{ client });
            }
            else {
                auto& list = ptr->second;
                size_t client_queue_size = std::numeric_limits<size_t>::max();
                for (auto &cur_client : list) {
                    auto count = cur_client->queue_size();
                    if (client_queue_size > count) {
                        client_queue_size = count;
                        client = cur_client;
                    }
                }
                if (!client || (client_queue_size > 1 && list.size() < maxcon_per_host)) {
                    client = std::make_shared<http_client>(make_strand(executor), host, port);
                    list.push_back(client);
                }
            }
        }

        client->enqueue(req);
    }

    http_pool_stats get_stats() {
        http_pool_stats stats;
        std::lock_guard lock(mutex);
        auto curr_time = system_clock::now();
        auto tmout = std::chrono::duration_cast<std::chrono::seconds>(curr_time - stats_time).count();
        auto reset = tmout > http_stats_tmout;
        if (reset) stats_time = curr_time;
        for (auto& ptr : clients) {
            stats.host_count++;
            for (auto& client : ptr.second) {
                auto client_stats = client->get_stats(reset);
                (client_stats.state > 0 ? stats.active_count : stats.inactive_count)++;
                stats.queue_size += client_stats.queue_size;
                stats.bytes_readed += client_stats.bytes_readed;
                stats.bytes_written += client_stats.bytes_written;
                stats.total_seconds += client_stats.total_seconds;
            }
        }
        if (stats.total_seconds > 0.) {
            stats.bandwidth = (stats.bytes_readed + stats.bytes_written) / stats.total_seconds;
        }
        return stats;
    }

private:
    system_clock::time_point stats_time = system_clock::now();
    size_t maxcon_per_host;
    asio_executor executor;
    std::mutex mutex;

    typedef std::list<http_client_ptr> clients_list;
    typedef std::map<std::string, clients_list> clients_map;
    clients_map clients;

};

}
