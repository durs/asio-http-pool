# Name
Asio HTTP Clients Pool.

# Description
HTTP clients pool for bulk requests in asynchronous mode.
This code is part of the TMS software.

# Usage
	``` C++    
    // declare asio thread pool and client http pool
    using namespace tms;
    static boost::asio::thread_pool io(4);
    static http_client_pool pool(io.get_executor(), 4);
    
    // process http get request
    pool.enqueue<http_string_body>("exemple.com", "80", "/test", nullopt, [](http_error err, http_stage stage, http_string_response&& resp){
        std::cout << "http ";
        if (stage >= http_stage_read) {
            std::cout << resp.result_int() << " " << resp.result();
        }
        if (err) {
            std::cout << " error[" << err << "] " << err.message();
        }
        std::cout << " body[len: " << resp.body().size() << "]"
    });
    
    
	```

# Additional
 * Simple asynchronous timer with loop mode
 * Universal URI parser template for char/wchar_t and std::string/std:string_view/boost::string_view
	``` C++
    uri_t<wchar_t> uri;

    // parse and apply only submitted parts
    uri.parse(L"///test?params");
    uri.parse(L"guest:demo@exemple.com");
    uri.parse(L"https:///temp");
    
    // ignore arguments, but apply 443 port by https prefix
    uri.set_defaults(L"http", L"localhost"); 
    
    // check result
    if (uri.str() != L"https://guest:demo@exemple.com:443/temp?params") {
        throw std::runtime_error("uri parse/split algoritms failed");
    };
	```
    
# Requires
 * C++14, C++17 or higher
 * Boost 1.75 or higher (1.71 already fails)
    
# Installation
 * Ubunta 20.XX    
 * Additional libraries (for modern Boost see bellow)
	``` bash
	sudo apt install \
		build-essential gdb \
		libboost-dev \
        libasio-dev \
		libssl-dev
	```
 * VS Code
	``` bash
	wget -q https://packages.microsoft.com/keys/microsoft.asc -O- | sudo apt-key add -
	sudo add-apt-repository "deb [arch=amd64] https://packages.microsoft.com/repos/vscode stable main"
    sudo apt update
    sudo apt install software-properties-common apt-transport-https wget
    sudo apt install code
	```
	Install plugin "C/C++ IntelliSense, debugging, and code browsing"

# Install modern boost
	``` bash
    wget https://boostorg.jfrog.io/artifactory/main/release/1.80.0/source/boost_1_80_0.tar.gz
    tar xvf boost_1_80_0.tar.gz
    cd boost_1_80_0
    ./bootstrap.sh --prefix=/usr/
    sudo ./b2 install
    ```