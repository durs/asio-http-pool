
#include <iostream>
#include <boost/url/parse.hpp>

#define ASIO_POOL_HTTPS_IGNORE
#include "../src/asio_http_pool.h"

#if (_WIN32 || _WIN64)
#ifndef ASIO_POOL_HTTPS_IGNORE
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")
#endif
#endif

using namespace tms;

static const int thread_count = 4;
static const int maxcon_per_host = 4;
static const int request_interval_msec = 1000;

static asio::thread_pool io(thread_count);
static http_client_pool pool(io.get_executor(), maxcon_per_host);
static std::string targets[] = {
#ifndef ASIO_POOL_HTTPS_IGNORE
    "https://databank.worldbank.org/data/download/SPI_EXCEL.zip",
#endif
    "http://index.okfn.org/place.html",
    "http://index.okfn.org/dataset.html",
    "http://index.okfn.org/download/opendataindex_data.zip"
};

void create_request() {
    static std::atomic<int> reqcnt = 0;
    static std::atomic<int> reqnum = 0;
    static auto targets_count = sizeof(targets) / sizeof(targets[0]);
    auto uri = boost::urls::parse_uri(targets[reqnum++ % targets_count]);
    auto host = uri->host();
    auto port = uri->port();
    auto path = uri->path();

    std::optional<https_method> https;
    if (uri->scheme() == "https") {
#ifndef ASIO_POOL_HTTPS_IGNORE
        https.emplace(https_method::tlsv12_client);
#endif
        if (port.empty()) port = "443";
    }
    else {
        if (port.empty()) port = "80";
    }
    
    reqcnt++;
    auto time = std::chrono::system_clock::now();
    pool.enqueue<http_string_body>(host, port, path, https, [time](http_error err, http_stage stage, http_string_response&& resp) {
        auto stats = pool.get_stats();
        std::cout << "["
            << "queue: " << --reqcnt << "/" << stats.queue_size << "; "
            << "connects: " << stats.active_count << "/" << (stats.active_count + stats.inactive_count) << "; "
            << (int)stats.bandwidth << " byte/s]";

        std::cout << " http ";
        if (stage >= http_stage_read) {
            std::cout << resp.result_int() << " " << resp.result();
        }
        if (err) {
            std::cout << " error[" << err << "] " << err.message();
            
        }
        else {
            std::chrono::duration<double> tm = std::chrono::system_clock::now() - time;
            auto &body = resp.body();
            auto data = body.data();
            auto size = body.size();
            std::cout << " body[len: " << size << "]"
                << " at " << (int)(tm.count() * 1000) << "ms";
        }

        std::cout << std::endl;
    });
};

int main() {
    std::cout << "==> started" << std::endl;

    // single first request
    create_request();

    // loop throw timeout
    auto timer = std::make_shared<async_timer>(io.get_executor());
    timer->wait(async_timer::milliseconds(request_interval_msec), [timer]() {
        create_request();
        timer->wait_next();
    });

    // wait infinity
    io.join();
}
