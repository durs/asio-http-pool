#pragma once
#include <string>
#include <deque>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/deadline_timer.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

using tcp = asio::ip::tcp;
using tcp_resolver = tcp::resolver;
using tcp_endpoints = tcp_resolver::results_type;
using tcp_endpoint = tcp_endpoints::endpoint_type;
using asio_executor = asio::any_io_executor;
using http_stream = beast::tcp_stream;
using http_error = beast::error_code;
using http_string = beast::string_view;
using http_verb = http::verb;

static const int http_connect_tmout = 30;
static const int http_write_tmout = 30;
static const int http_read_tmout = 60;

enum http_stage {
    http_stage_none = 0,
    http_stage_resolve = 1,
    http_stage_connect = 2,
    http_stage_write = 3,
    http_stage_read = 4,
    http_stage_complete = 5,
};

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
        http::async_write(stream, request, handler);
    }
    virtual void read(http_stream& stream, process_handler_type handler) {
        http::async_read(stream, buffer, response, handler);
    }
    virtual void end(http_error err, http_stage stage) {
        std::move(handler)(err, stage, std::forward<response_type>(response));
    }
protected:
    beast::flat_buffer buffer;
    handler_type handler;
};

typedef http::response<http::vector_body<uint8_t>> http_binary_response;
typedef http::response<http::string_body> http_json_response;

template<typename handler_type>
class http_binary_get : public http_request_t<http::empty_body, http::vector_body<uint8_t>, handler_type> {
public:
    http_binary_get(http_string target, handler_type h)
        : http_request_t<http::empty_body, http::vector_body<uint8_t>, handler_type>(http_verb::get, std::move(target), std::move(h))
    {
        // request.set(http::field::content_type, "application/json");
        //request.set(http::field::accept, "application/json");
    }
};

template<typename handler_type>
class http_json_get: public http_request_t<http::empty_body, http::string_body, handler_type> {
public:
    http_json_get(http_string target, handler_type h)
       : http_request_t<http::empty_body, http::string_body, handler_type>(http_verb::get, std::move(target), std::move(h))
    {
        this->request.set(http::field::accept, "application/json");
    }    
};

template<typename handler_type>
class http_json_post : public http_request_t<http::string_body, http::string_body, handler_type> {
public:
    http_json_post(http_string target, std::string data, handler_type h)
        : http_request_t<http::string_body, http::string_body, handler_type>(http_verb::post, std::move(target), std::move(data), std::move(h))
    {
        this->request.set(http::field::content_type, "application/json");
        this->request.set(http::field::accept, "application/json");
    }
};

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

class http_client:
    public std::enable_shared_from_this<http_client>
{
public:
    explicit http_client(const asio_executor &ex, std::string_view _host, std::string_view _port)
        : executor(ex), resolver(ex), timer(ex), host(_host), port(_port)
    {}

    template<typename Request>
    inline void enqueue(Request* &&req) {
        enqueue(http_request_ptr(std::move(req)));
    }
    inline void enqueue(http_request_ptr req) {
        asio::post(executor, std::bind(&http_client::append, shared_from_this(), std::move(req)));
    }

private:
    asio_executor executor;
    tcp_resolver resolver;
    async_timer timer;
    std::string host, port;
    std::optional<http_stream> stream;
    std::deque<http_request_ptr> requests;
    int trycnt = 0;

    bool checked(http_error err, http_stage stage) {

        // on error reset stream
        if (err) {
            stream.reset();

            // may be stream closed, reconnect and try again
            if (trycnt == 0 && (stage == http_stage_write || stage == http_stage_read)) {
                trycnt++;
                asio::post(executor, std::bind(&http_client::next, shared_from_this()));
                return false;
            }
        }

        // complete current request
        if (stage == http_stage_complete || err) {
            trycnt = 0;
            this->complete(err, stage);
        }

        return !err.failed();
    }

    void complete(http_error err, http_stage stage) {
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

    inline void keep_alive(const http_string value) {
        int timeout = 0;
        if (!value.empty()) {
            size_t pos = value.find("timeout=");
            if (pos != decltype(value)::npos) {
                timeout = atoi(&value[pos + 8]);
            }
        }
        keep_alive(timeout);
    }

    inline void keep_alive(int timeout = 0) {
        if (timeout > 0) {
            auto self = shared_from_this();
            timer.wait(async_timer::seconds(timeout), [self]() {
                self->close();
            });
        }
        else {
            timer.cancel();
        }
    }

    inline void close() {
        if (requests.empty() && stream.has_value()) {
            stream->socket().shutdown(tcp::socket::shutdown_both);
            stream.reset();
        }
    }

    void append(http_request_ptr req) {
        requests.push_back(std::move(req));
        if (requests.size() == 1) {
            init();
        }
    }

    void next() {
        if (!requests.empty()) {
            init();
        }
    }

    void init() {
        if (stream.has_value()) {
            auto& socket = stream->socket();
            if (socket.is_open()) { // always is open
                keep_alive(0);
                return send();
            }
        }
        keep_alive(0);
        stream.emplace(executor);
        auto self = shared_from_this();
        resolver.async_resolve(host, port, [self](http_error err, tcp_endpoints endpoints) {
            if (!self->checked(err, http_stage_resolve)) return;

            auto& stream = beast::get_lowest_layer(*self->stream);
            stream.expires_after(std::chrono::seconds(http_connect_tmout));
            stream.async_connect(endpoints, [self](http_error err, tcp_endpoint endpoint) {
                if (!self->checked(err, http_stage_connect)) return;
                self->send();
            });
        });
    }

    void send() {
        if (requests.empty()) {
            return;
        }
        auto& stream = *this->stream;
        auto self = shared_from_this();
        auto req = requests.front();
        if (trycnt == 0) {
            req->set("host", host);
            req->set("connection", "keep-alive");
            req->set("user-agent", BOOST_BEAST_VERSION_STRING);
        }
        stream.expires_after(std::chrono::seconds(http_write_tmout));
        req->write(stream, [self, req](http_error err, size_t transferred) {
            if (!self->checked(err, http_stage_write)) return;

            auto& stream = *self->stream;
            stream.expires_after(std::chrono::seconds(http_read_tmout));
            req->read(stream, [self, req](http_error err, size_t transferred) {
                self->checked(err, err ? http_stage_read : http_stage_complete);
            });
        });
    }
};