#pragma once
#include <list>
#include <map>
#include <deque>
#include <mutex>
#include <string>
#include <sstream>
#include <chrono>
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

#ifdef _MSVC_LANG
#define CPP_LANG_VERSION _MSVC_LANG
#else 
#define CPP_LANG_VERSION __cplusplus
#endif

#if (CPP_LANG_VERSION < 201402L)

#error not supported c++ standard

#elif (CPP_LANG_VERSION < 201703L)

#include <boost/none.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_view_fwd.hpp>

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

    // common type declarations
#if (CPP_LANG_VERSION < 201703L)
    template<typename T> using optional = boost::optional<T>;
    template<typename T> using basic_string_view = boost::basic_string_view<T, std::char_traits<T> >;
    using string_view = boost::string_view;
    using wstring_view = boost::wstring_view;
    static const auto nullopt = boost::none;
#else
    template<typename T> using optional = std::optional<T>;
    template<typename T> using basic_string_view = std::basic_string_view<T, std::char_traits<T> >;
    using string_view = std::string_view;
    using wstring_view = std::wstring_view;
    static const auto nullopt = std::nullopt;
#endif

    // nullstr template declarations (may be unused)
    template<typename char_type>
    inline constexpr basic_string_view<char_type> emptystr() { return basic_string_view<char_type>(); }
    template<typename char_type>
    static const auto nullstr = emptystr<char_type>();

    // http stages
    enum http_stage {
        http_stage_none = 0,
        http_stage_resolve = 1,
        http_stage_connect = 2,
        http_stage_handshake = 3,
        http_stage_write = 4,
        http_stage_read = 5,
        http_stage_complete = 6
    };
}
