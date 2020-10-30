#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <chrono>
#include <unordered_map>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "utils.h"


namespace chrono = std::chrono;
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
namespace uuids = boost::uuids;         // from <boost/uuid/uuid.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


const ParsedURI& uri = parseURI("wss://test.deribit.com/ws/api/v2");


const std::string GetJsonText(const rapidjson::Document& d)
{
    rapidjson::StringBuffer buffer;

    // buffer.Clear();
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    d.Accept(writer);

    return std::string(buffer.GetString(), buffer.GetSize());
}


// Sends a WebSocket message and prints the response
int main(int argc, char** argv)
{
    try
    {       
        // -------------------------------------------------------------------
        //          CREATING A WEBSTOCKET AND CONNECTING TO THE HOST
        // -------------------------------------------------------------------

        // The io_context is required for all I/O
        net::io_context ioc;

        // The SSL context is required, and holds certificates
        ssl::context ctx{ ssl::context::tls };
        ctx.set_default_verify_paths();
        ctx.set_options(ssl::context::default_workarounds);

        // These objects perform our I/O
        tcp::resolver resolver{ ioc };
        websocket::stream<beast::ssl_stream<tcp::socket>> ws{ ioc, ctx };

        // Look up the domain name
        auto const results = resolver.resolve(uri.domain, uri.port);

        // Make the connection on the IP address we get from a lookup
        auto ep = net::connect(get_lowest_layer(ws), results);

        // Update the host_ string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        // See https://tools.ietf.org/html/rfc7230#section-5.4
        std::string host = uri.domain + ':' + std::to_string(ep.port());

        // Perform the SSL handshake
        ws.next_layer().handshake(ssl::stream_base::client);

        // Set a decorator to change the User-Agent of the handshake
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req)
            {
                req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                    " websocket-client-coro");
            }));

        // Perform the websocket handshake
        ws.handshake(host, uri.resource);

        // -------------------------------------------------------------------

        // 'State' variables
        uuids::random_generator uuid_gen;
        std::unordered_map<std::string, std::unique_ptr<rapidjson::Document>> prev_requests;

        // 'reusable' variables
        std::unique_ptr<rapidjson::Document> request;
        rapidjson::Document response;

        auto send_msg = [&uuid_gen, &prev_requests, &ws](
            const std::string& method,
            const std::unordered_map<std::string, std::string>& params)
        {
            std::unique_ptr<rapidjson::Document> d(new rapidjson::Document());
            rapidjson::Document::AllocatorType& alloc = d->GetAllocator();
            std::string id = uuids::to_string(uuid_gen());

            d->SetObject();
            d->AddMember("jsonrc", "2.0", alloc);
            d->AddMember("id", rapidjson::Value(id.c_str(), alloc), alloc);
            d->AddMember("method", rapidjson::Value(method.c_str(), alloc), alloc);

            rapidjson::Value v_params(rapidjson::kObjectType);

            for (auto& it : params)
            {
                v_params.AddMember(
                    rapidjson::Value(it.first.c_str(), alloc),
                    rapidjson::Value(it.second.c_str(), alloc),
                    alloc);
            }

            d->AddMember("params", v_params, alloc);
            
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            
            d->Accept(writer);
            
            const std::string& msg = std::string(buffer.GetString(), buffer.GetSize());
            
            prev_requests.insert(std::make_pair(id, std::move(d)));
            
            ws.write(net::buffer(msg));
        };

        auto recv_msg = [&ws](rapidjson::Document& d)
        {
            beast::flat_buffer buffer;     // This buffer will hold the incoming message
            ws.read(buffer);               // Read a message into our buffer

            const std::string& msg = beast::buffers_to_string(buffer.data());
            d.Parse(msg.c_str());
        };

        // Initial requests
        send_msg("public/get_time", {});
        send_msg("public/set_heartbeat", { {"interval", "10"} });
        
        // Eternal loop
        while (ws.is_open())
        {
            // Receiving the message
            // it waits if needed
            recv_msg(response);

            // checking the response type
            if (response.HasMember("method") && response["method"].IsString())
            {
                std::string method = response["method"].GetString();

                if (method == "subscription")
                {
                    // todo later...
                }
                else if (method == "heartbeat")
                {
                    std::string type = response["params"]["type"].GetString();
                    if (type == "test_request")
                    {
                        send_msg("public/test", {});
                        send_msg("public/get_time", {});
                    }
                }
            }
            else if (response.HasMember("result"))
            {
                auto it = prev_requests.find(response["id"].GetString());
                request = std::move(it->second);
                prev_requests.erase(it);

                std::string method = (*request.get())["method"].GetString();

                if (method == "public/get_time")
                {
                    long t_system = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
                    long t_server = response["result"].GetInt64();
                    std::cout << "System time: " << t_system << std::endl;
                    std::cout << "Server time: " << t_server << std::endl;
                    std::cout << std::endl;
                }
            }
            else if (response.HasMember("error"))
            {
                // todo later...
            }
        }

        // Close the WebSocket connection
        ws.close(websocket::close_code::normal);
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}