#include "connection.hpp"
#include <boost/algorithm/string.hpp> // for case-insensitive string comparison
#include <regex>


URI parseURI(const std::string& url) {
    URI result;
    auto value_or = [](const std::string& value, std::string&& deflt) -> std::string {
        return (value.empty() ? deflt : value);
    };
    // Note: only "http", "https", "ws", and "wss" protocols are supported
    static const std::regex PARSE_URL{ R"((([httpsw]{2,5})://)?([^/ :]+)(:(\d+))?(/([^ ?]+)?)?/?\??([^/ ]+\=[^/ ]+)?)",
                                       std::regex_constants::ECMAScript | std::regex_constants::icase };
    std::smatch match;
    if (std::regex_match(url, match, PARSE_URL) && match.size() == 9) {
        result.protocol = value_or(boost::algorithm::to_lower_copy(std::string(match[2])), "http");
        result.domain = match[3];
        const bool is_sequre_protocol = (result.protocol == "https" || result.protocol == "wss");
        result.port = value_or(match[5], (is_sequre_protocol) ? "443" : "80");
        result.resource = value_or(match[6], "/");
        result.query = match[8];
        assert(!result.domain.empty());
    }
    return result;
}

// Sends a WebSocket message and prints the response
WSSession::WSSession(const URI& uri)
{
    // The SSL context is required, and holds certificates
    ssl::context ctx{ ssl::context::tlsv12_client };

    // This holds the root certificate used for verification
    ctx.set_default_verify_paths();
    ctx.set_options(ssl::context::default_workarounds);

    m_ws = std::unique_ptr<tcp_websocket>(new tcp_websocket(ioc, ctx));
    tcp::resolver resolver{ ioc };

    // Look up the domain name
    auto const results = resolver.resolve(uri.domain, uri.port);

    // Make the connection on the IP address we get from a lookup
    auto ep = get_lowest_layer(*m_ws.get()).connect(results);

    // Perform the SSL handshake
    m_ws->next_layer().handshake(ssl::stream_base::client);

    // Set a decorator to change the User-Agent of the handshake
    m_ws->set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req)
        {
            req.set(http::field::user_agent,
                std::string(BOOST_BEAST_VERSION_STRING) +
                " websocket-client-coro");
        }));

    // Perform the websocket handshake
    m_ws->handshake(
        uri.domain + ':' + std::to_string(ep.port()),
        uri.resource
    );
}

WSSession::~WSSession()
{
    // Close the WebSocket connection
    m_ws->close(websocket::close_code::normal);
}

void WSSession::send(std::string msg)
{
    m_ws->write(net::buffer(msg));
}

std::string WSSession::recv()
{
    beast::flat_buffer buffer;     // This buffer will hold the incoming message
    m_ws->read(buffer);            // Read a message into our buffer
    return beast::buffers_to_string(buffer.data());
}

bool WSSession::is_open()
{
    return m_ws->is_open();
}

