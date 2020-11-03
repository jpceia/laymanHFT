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
#include <math.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include <boost/variant.hpp>

#include "utils.hpp"
#include "book.hpp"


namespace chrono = std::chrono;
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
namespace uuids = boost::uuids;         // from <boost/uuid/uuid.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

const std::string interval = "100ms";


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

        // Check command line arguments.
        if (argc < 2)
        {
            std::cerr << "Usage: layman-hft uri instrument [client_id] [client_secret]\n" << std::endl;
            return EXIT_FAILURE;
        }

        const ParsedURI& uri = parseURI(argv[1]);
        const std::string& instrument_name = argv[2];
        const std::string& client_id = argc > 3 ? argv[3] : "";
        const std::string& client_secret = argc > 4 ? argv[4] : "";

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

        const std::string book_channel = "book." + instrument_name + "." + interval;
        const std::string changes_channel = "user.changes." + instrument_name + "." + interval;

        // 'State' variables
        uuids::random_generator uuid_gen;
        std::string refresh_token, access_token;
        std::unordered_map<std::string, std::unique_ptr<rapidjson::Document>> prev_requests;
        long prev_change_id;
        double position_usd = std::nan(NULL);
        Bids bids;
        Asks asks;

        // 'reusable' variables
        std::unique_ptr<rapidjson::Document> request;
        rapidjson::Document response;

        auto send_msg = [&uuid_gen, &prev_requests, &ws](
            const std::string& method,
            const std::unordered_map<
                std::string,
                boost::variant<std::string, double, std::vector<std::string>>
            >& params)
        {
            std::unique_ptr<rapidjson::Document> d(new rapidjson::Document());
            rapidjson::Document::AllocatorType& alloc = d->GetAllocator();
            std::string id = uuids::to_string(uuid_gen());

            d->SetObject();
            d->AddMember("jsonrc", "2.0", alloc);
            d->AddMember("id", rapidjson::Value(id.c_str(), alloc), alloc);
            d->AddMember("method", rapidjson::Value(method.c_str(), alloc), alloc);

            rapidjson::Value o_params(rapidjson::kObjectType);

            for (auto& it : params)
            {
                if (it.second.which() == 0) // string
                {
                    o_params.AddMember(
                        rapidjson::Value(it.first.c_str(), alloc),
                        rapidjson::Value(boost::get<std::string>(it.second).c_str(), alloc),
                        alloc);
                }
                else if (it.second.which() == 1) // double
                {
                    o_params.AddMember(
                        rapidjson::Value(it.first.c_str(), alloc),
                        rapidjson::Value(boost::get<double>(it.second)),
                        alloc);
                }
                else // vector<string>
                {
                    const std::vector<std::string>& v = boost::get<std::vector<std::string>>(it.second);
                    rapidjson::Value v_params(rapidjson::kArrayType);
                    for (auto& jt : v)
                    {
                        v_params.PushBack(rapidjson::Value(jt.c_str(), alloc), alloc);
                    }
                    o_params.AddMember(
                        rapidjson::Value(it.first.c_str(), alloc),
                        v_params,
                        alloc);
                }
            }

            d->AddMember("params", o_params, alloc);
            
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

        // -------------------------------------------------------------------
        //                          INITIAL REQUEST
        // -------------------------------------------------------------------

        // Requesting authorizationand refresh tokens
        if (!client_id.empty())
        {
            send_msg("public/auth", {
                    {"grant_type", "client_credentials"},
                    {"client_id", client_id},
                    {"client_secret", client_secret}
                });

            send_msg("private/get_position", { {"instrument_name", instrument_name } });
        }

        // Requesting time from the API platform
        send_msg("public/get_time", {});

        // setting heartbeat to check life
        send_msg("public/set_heartbeat", { {"interval", "10"} });

        // subscribing channel with book information
        send_msg("public/subscribe", { {"channels", std::vector<std::string>({book_channel, changes_channel}) } });

        // -------------------------------------------------------------------
        //                                LOOP
        // -------------------------------------------------------------------

        while (ws.is_open())
        {
            // Receiving the message
            // it waits if needed
            recv_msg(response);

            // checking the response type
            if (response.HasMember("method") && response["method"].IsString())
            {
                const std::string& method = response["method"].GetString();
                auto& params = response["params"];

                if (method == "subscription")
                {
                    const std::string& channel = params["channel"].GetString();
                    auto& data = params["data"];

                    // -------------------------------------------------------
                    // Book updates
                    // -------------------------------------------------------
                    if (channel == book_channel)
                    {
                        long change_id = data["change_id"].GetInt64();
                        if (data.HasMember("prev_change_id"))
                        {
                            long msg_prev_change_id = data["prev_change_id"].GetInt64();
                            if (msg_prev_change_id != prev_change_id)
                            {
                                throw std::exception("Invalid change_id sequence");
                            }
                        }
                        prev_change_id = change_id;

                        asks.apply_changes(data["asks"]);
                        bids.apply_changes(data["bids"]);
                    }
                    // -------------------------------------------------------
                    // Changes updates
                    // -------------------------------------------------------
                    else if (channel == changes_channel)
                    {
                    }
                }
                else if (method == "heartbeat")
                {
                    const std::string& type = response["params"]["type"].GetString();
                    if (type == "test_request")
                    {
                        send_msg("public/test", {});
                        send_msg("public/get_time", {});
                    }
                }
            }
            else if (response.HasMember("result"))
            {
                const auto& it = prev_requests.find(response["id"].GetString());
                request = std::move(it->second);
                prev_requests.erase(it);

                const std::string& method = (*request.get())["method"].GetString();
                const auto& result = response["result"];


                // -----------------------------------------------------------
                // private / get_position
                // -----------------------------------------------------------

                else if (method == "private/get_position")
                {
                    assert(result["instrument_name"].GetString() == instrument_name);
                    const double& server_position_usd = result["size"].GetDouble();
                    if (isnan(position_usd))
                    {
                        position_usd = server_position_usd;
                        std::cout << "Initial position: " << position_usd << std::endl;
                    }
                    else if (position_usd != server_position_usd)
                    {
                        throw std::exception("Position (USD) mismatch");
                    }
                }

                // -----------------------------------------------------------
                // public / get_time
                // -----------------------------------------------------------

                else if (method == "public/get_time")
                {
                    long t_system = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
                    long t_server = result.GetInt64();
                    std::cout << "System time: " << t_system << std::endl;
                    std::cout << "Server time: " << t_server << std::endl;
                    std::cout << std::endl;
                }

                // -----------------------------------------------------------
                // public / auth
                // -----------------------------------------------------------

                else if (method == "public/auth")
                {
                    std::cout << "Receiving new authorization tokens." << std::endl;
                    refresh_token = result["refresh_token"].GetString();
                    access_token = result["access_token"].GetString();
                }
            }
            else if (response.HasMember("error"))
            {
                prev_requests.erase(response["id"].GetString());

                const auto& error = response["error"];
                int code = error["code"].GetInt();

                std::cout << "Received error message: (" << code << ") " << error["message"].GetString() << std::endl;

                if (code == 13009)
                {
                    std::cout << "Expired access_token, requesting a new one." << std::endl;
                    send_msg("public_auth", {
                            {"grant_type", "refresh_token"},
                            {"refresh_token", refresh_token}
                        });
                }
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