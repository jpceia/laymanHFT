#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <string>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

typedef websocket::stream<beast::ssl_stream<beast::tcp_stream>> tcp_websocket;


struct URI {
    std::string protocol;
    std::string domain;  // only domain must be present
    std::string port;
    std::string resource;
    std::string query;   // everything after '?', possibly nothing
};

URI parseURI(const std::string&);

// Sends a WebSocket message and prints the response
class WSSession : public std::enable_shared_from_this<WSSession>
{
    std::shared_ptr<tcp_websocket> _ws;
    net::io_context _ioc;

public:
    // Resolver and socket require an io_context
    explicit WSSession(const URI& uri);
    ~WSSession();

    void send(std::string msg);
    std::string recv();
    bool is_open();
};