#pragma once
#include "http_base.h"

namespace tms {

    //---------------------------------------------------------------------------------------------
    // simple async timer

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
    // URI parsing/split template class

    template<typename char_type> struct uri_tags {
        using string = std::basic_string<char_type, std::char_traits<char_type>, std::allocator<char_type> >;
        static const string empty;
        static const string https;
        static const string p443;
        static const string p80;
    };

    template<> const std::string uri_tags<char>::empty("");
    template<> const std::string uri_tags<char>::https("https");
    template<> const std::string uri_tags<char>::p443("443");
    template<> const std::string uri_tags<char>::p80("80");

    template<> const std::wstring uri_tags<wchar_t>::empty(L"");
    template<> const std::wstring uri_tags<wchar_t>::https(L"https");
    template<> const std::wstring uri_tags<wchar_t>::p443(L"443");
    template<> const std::wstring uri_tags<wchar_t>::p80(L"80");

    template <
        typename char_type, 
        typename string_type = basic_string_view<char_type>
    >
    struct uri_t {
        using tags = uri_tags<char_type>;

        string_type scheme;
        string_type host, port;
        string_type user, pswd;
        string_type fullpath, path, args;

        void set_defaults(
            string_type _scheme = tags::empty, 
            string_type _host = tags::empty, 
            string_type _port = tags::empty, 
            string_type _user = tags::empty, 
            string_type _pswd = tags::empty, 
            string_type _path = tags::empty, 
            string_type _args = tags::empty
        ) {
            if (scheme.empty()) scheme = std::move(_scheme);
            if (host.empty()) host = std::move(_host);
            if (user.empty()) user = std::move(_user);
            if (pswd.empty()) pswd = std::move(_pswd);
            if (path.empty()) path = std::move(_path);
            if (args.empty()) args = std::move(_args);
            if (port.empty()) {
                if (!_port.empty()) port = std::move(_port);
                else if (scheme == tags::https) port = tags::p443;
                else port = tags::p80;
            }
        }

        void parse(const string_type &url) {
            if (!url.empty()) {
                parse(url.begin(), url.end());
            }
        }

        template<typename const_iterator>
        void parse(const_iterator url, const_iterator url_end) {
            size_t len;
 
            // extract protocol "http://xxx/xxx"
            auto path_ptr = std::find(url, url_end, '/');
            if (path_ptr != url_end) {
                if (*(path_ptr + 1) == '/') {
                    
                    // store protocol or save default
                    if ((len = path_ptr - url) > 1 && *(path_ptr - 1) == ':') {
                        scheme = string_type(&*url, len - 1);
                    }

                    // prepare next
                    url = path_ptr + 2;
                    path_ptr = std::find(url, url_end, '/');
                }
            }

            // extract user "user:pswd@xxx/xxx"
            auto host_ptr = std::find(url, path_ptr, '@');
            if (host_ptr != path_ptr) {

                // extract password
                auto pswd_ptr = std::find(url, host_ptr, ':');
                if (pswd_ptr != host_ptr) {
                    pswd = string_type(&*(pswd_ptr + 1), host_ptr - pswd_ptr - 1);
                }

                // clear default passsword
                else {
                    pswd = {};
                }

                // store user
                user = string_type(&*url, pswd_ptr - url);
                host_ptr++;
            }
            else {
                // save default user
                host_ptr = url;
            }

            // extract port "host:port/xxx"
            auto port_ptr = std::find(host_ptr, path_ptr, ':');
            if (port_ptr != path_ptr) {

                // store port or save default
                if ((len = path_ptr - port_ptr) > 1) {
                    port = string_type(&*(port_ptr + 1), len - 1);
                }
            }

            // store host or save default
            if ((len = port_ptr - host_ptr) > 0) {
                host = string_type(&*host_ptr, len);
            }

            // extract path
            if (path_ptr != url_end) {
                fullpath = string_type(&*path_ptr, url_end - path_ptr);
                auto args_ptr = std::find(path_ptr + 1, url_end, '?');
                if (args_ptr != url_end) {

                    // store args
                    if ((len = url_end - args_ptr) > 1) {
                        args = string_type(&*(args_ptr + 1), len - 1);
                    }

                    // or clear default
                    else {
                        args = {};
                    }
                }

                // store path or save default
                if ((len = args_ptr - path_ptr) > 0) {
                    path = string_type(&*path_ptr, len);
                }
            }
        }

        std::basic_string<char_type, std::char_traits<char_type> > str() {
            std::basic_stringstream<char_type, std::char_traits<char_type>, std::allocator<char_type> > strm;
            if (scheme.empty()) strm << "//";
            else strm << scheme << "://";
            if (!user.empty()) {
                strm << user;
                if (!pswd.empty()) strm << ":" << pswd;
                strm << "@";
            }
            if (host.empty()) strm << "localhost";
            else strm << host;
            if (!port.empty()) strm << ":" << port;
            if (!path.empty()) {
                if (path[0] != '/') strm << '/';
                strm << path;
            }
            if (!args.empty()) {
                if (args[0] != '?') strm << '?';
                strm << args;
            }
            return strm.str();
        }  

        static bool test() {
            uri_t<wchar_t> uri;
            uri.parse(L"///test?params");
            uri.parse(L"guest:demo@exemple.com");
            uri.parse(L"https:///temp");
            uri.set_defaults(L"http", L"localhost");
            if (uri.str() != L"https://guest:demo@exemple.com:443/temp?params") {
                throw std::runtime_error("uri parse/split algoritms failed");
            };
        }
    };

    // declare common specificated uri_t template types
    typedef uri_t<char, string_view> uri_view;
    typedef uri_t<char, std::string> uri_str;
    typedef uri_t<wchar_t, wstring_view> uri_wview;
    typedef uri_t<wchar_t, std::wstring> uri_wstr;

    //---------------------------------------------------------------------------------------------
}