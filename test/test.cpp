#ifdef _MSC_VER
#pragma warning(disable:4503)
#endif

#include <iostream>

#ifndef ASIO_POOL_HTTPS_IGNORE
#define ASIO_POOL_HTTPS_IGNORE
#endif

#include "../src/http_pool.h"

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
static const std::string targets[] = {
#ifndef ASIO_POOL_HTTPS_IGNORE
    "https://databank.worldbank.org/data/download/SPI_EXCEL.zip",
#endif
    "http://index.okfn.org/place.html",
    "http://index.okfn.org/dataset.html",
    "http://index.okfn.org/download/opendataindex_data.zip"
};

void create_request() {
    static std::atomic<int> reqcnt(0);
    static std::atomic<int> reqnum(0);
    static auto targets_count = sizeof(targets) / sizeof(targets[0]);
    auto target = targets[reqnum++ % targets_count];
    uri_t<char, http_string> uri;
    uri.set_defaults("http", "localhost");
    uri.parse(target);
    auto uristr = uri.str();

    optional<https_method> https;
    if (uri.scheme == "https") {
#ifndef ASIO_POOL_HTTPS_IGNORE
        https.emplace(https_method::tls_client);
#endif
    }
    
    reqcnt++;
    auto time = std::chrono::system_clock::now();
    pool.enqueue<http_string_body>(uri.host, uri.port, uri.fullpath, https, [time, uristr](http_error err, http_stage stage, http_string_response&& resp) {        
        std::cout << ">>> " + uristr << std::endl;
        
        std::cout << "    http ";
        if (stage >= http_stage_read) {
            std::cout << resp.result_int() << " " << resp.result();
        }
        if (err) {
            std::cout << "error[" << err << "] " << err.message();
        }
        else {
            std::chrono::duration<double> tm = std::chrono::system_clock::now() - time;
            auto& body = resp.body();
            auto size = body.size();
            std::cout << " body[len: " << size << "]"
                << " at " << (int)(tm.count() * 1000) << "ms";
        }
        std::cout << std::endl;

        auto stats = pool.get_stats();
        std::cout << "    queue: " << --reqcnt << "/" << stats.queue_size 
            << "; connects: " << stats.active_count << "/" << (stats.active_count + stats.inactive_count) 
            << "; errors: " << stats.error_count
            << "; " << (stats.bandwidth / 1024) << " KB/s]";

        std::cout << std::endl << std::endl;
    });
};

int main() {
    std::cout << "==> started" << std::endl;

    // set global timeouts
    http_timeouts::connect = 60;

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
