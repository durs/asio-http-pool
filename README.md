# Name
Asio HTTP Clients Pool

# Description
HTTP clients pool for bulk requests in asynchronous mode

# Additional
 * Simple asynchronous timer with loop mode
 * Universal URI parser template for char/wchar_t and std::string/std:string_view/boost::string_view

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
	Install plugin "C/C++ IntelliSense, debugging, and code browsing."

# Install modern boost
	``` bash
    wget https://boostorg.jfrog.io/artifactory/main/release/1.80.0/source/boost_1_80_0.tar.gz
    tar xvf boost_1_80_0.tar.gz
    cd boost_1_80_0
    ./bootstrap.sh --prefix=/usr/
    sudo ./b2 install
    ```