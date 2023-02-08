#pragma once
#include "http_base.h"
#include "http_request.h"
#include "http_utils.h"

namespace tms {

    /*
    enum class http_timeouts {
        connect = 30,
        write = 30,
        read = 60,
        keep = 60,
        stats = 30
    };
    //*/
    template<typename T = void> struct http_timeouts_t {
        static int connect;
        static int write;
        static int read;
        static int keep;
        static int stats;
    };
    template<typename T> int http_timeouts_t<T>::connect = 30;
    template<typename T> int http_timeouts_t<T>::write = 30;
    template<typename T> int http_timeouts_t<T>::read = 60;
    template<typename T> int http_timeouts_t<T>::keep = 60;
    template<typename T> int http_timeouts_t<T>::stats = 30;
    using http_timeouts = http_timeouts_t<>;

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
        
        explicit http_client(const asio_executor& ex, http_string _host, http_string _port)
            : executor(ex), resolver(ex), timer(ex), host(_host), port(_port)
        {}

#ifndef ASIO_POOL_HTTPS_IGNORE
        explicit http_client(const asio_executor& ex, http_string _host, http_string _port, https_method _method)
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
            std::lock_guard<std::mutex> lock(mutex);
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
            std::lock_guard<std::mutex> lock(mutex);
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
            if (timeout <= 0) timeout = http_timeouts::keep;
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
                stream.expires_after(http_timeouts::write);
                req->write(stream, beast::bind_front_handler(&http_client::on_write, self));
            }
        }

        void on_resolve(http_error err, tcp_endpoints endpoints) {
            if (check_result(err, http_stage_resolve)) {
                auto self = shared_from_this();            
                stream.expires_after(http_timeouts::connect);
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
                stream.expires_after(http_timeouts::read);
                req->read(stream, beast::bind_front_handler(&http_client::on_read, self));
            }
        }

        void on_read(http_error err, size_t transferred) {
            check_result(err, err ? http_stage_read : http_stage_complete, transferred);
        }
    };

    typedef std::shared_ptr<http_client> http_client_ptr;

}
