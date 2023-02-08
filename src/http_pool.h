#pragma once
#include "http_base.h"
#include "http_client.h"

namespace tms {

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
        inline void enqueue(http_string host, http_string port, http_string path, optional<https_method> https, handler_type handler) {
            using request_type = http_request_t<http_empty_body, response_body_type, handler_type>;
            auto req = std::make_shared<request_type>(http_verb::get, std::move(path), std::move(handler));
            enqueue(std::move(host), std::move(port), https, std::move(req));
        }

        template<typename response_body_type = http_binary_body, typename handler_type>
        inline void enqueue(http_string host, http_string port, http_string path, std::string data, optional<https_method> https, handler_type handler) {
            using request_type = http_request_t<http_string_body, response_body_type, handler_type>;
            auto req = std::make_shared<request_type>(http_verb::get, std::move(path), std::move(data), std::move(handler));
            enqueue(std::move(host), std::move(port), https, std::move(req));
        }

        template<typename request_type>
        inline void enqueue(http_string host, http_string port, optional<https_method> https, request_type*&& req) {
            enqueue(std::move(host), std::move(port), https, http_request_ptr(std::move(req)));
        }

        inline void enqueue(http_string host, http_string port, http_request_ptr req) {
            return enqueue(std::move(host), std::move(port), nullopt, std::move(req));
        }

        inline void enqueue(http_string host, http_string port, optional<https_method> https, http_request_ptr req) {
            std::string key(host);
            if (!port.empty()) {
                key += ":" + std::string(port);
            }
    #ifndef ASIO_POOL_HTTPS_IGNORE
            if (https) {
                key += ":ssl" + std::to_string(*https);
            }
    #endif

            http_client_ptr client;
            {
                std::lock_guard<std::mutex> lock(mutex);
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
            std::lock_guard<std::mutex> lock(mutex);
            auto curr_time = system_clock::now();
            auto tmout = std::chrono::duration_cast<std::chrono::seconds>(curr_time - stats_time).count();
            auto reset = tmout > http_timeouts::stats;
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
