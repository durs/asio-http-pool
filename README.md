# Name
Asio HTTP Clients Pool

# Description
HTTP clients pool for bulk requests in asynchronous mode

# Additional
 * Simple asynchronous timer with loop mode
 * Universal URI parser template for char/wchar_t and std::string/std:string_view/boost::string_view

# Requires
 * C++14, C++17 or higher
 * Boost 1.75 or higher (1.69 already fails)
    
# Installation
 * Ubunta 20.XX    
 * Additional libraries (Intel TBB, ZeroC Ice, RabbitMQ, Boost)
	``` bash
	sudo apt install \
		build-essential gdb \
		libboost-dev \
        libboost-date-time-dev \
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



