#include <cstdlib>
#include <iostream>
#include <string>
#include <chrono>
#include <unordered_map>
#include <assert.h>
#include <math.h>
#include <ctime>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include <boost/variant.hpp>

#include "connection.hpp"
#include "book.hpp"

#define CLIP(x, a, b) x > a ? (x < b ? x : b) : a

namespace chrono = std::chrono;
namespace uuids = boost::uuids;         // from <boost/uuid/uuid.hpp>

const double MIN_DEPTH_QTY = 500;
const double MID_DEPTH_QTY = 2000;
const double MAX_DEPTH_QTY = 20000;
const double ORDER_AMOUNT = 5000;
const double MAX_POSITION_USD = 50000;
const std::string interval = "raw";


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

        const URI& uri = parseURI(argv[1]);
        const std::string& instrument_name = argv[2];
        const std::string& client_id = argc > 3 ? argv[3] : "";
        const std::string& client_secret = argc > 4 ? argv[4] : "";

        // -------------------------------------------------------------------
        //          CREATING A WEBSTOCKET AND CONNECTING TO THE HOST
        // -------------------------------------------------------------------

        WSSession ws{ uri };

        // -------------------------------------------------------------------

        const std::string book_channel = "book." + instrument_name + "." + interval;
        const std::string changes_channel = "user.changes." + instrument_name + "." + interval;

        // 'State' variables
        uuids::random_generator uuid_gen;
        std::string refresh_token, access_token;
        std::unordered_map<std::string, std::unique_ptr<rapidjson::Document>> prev_requests;
        long prev_change_id = 0;
        double position_usd = NAN;
        double current_buy_price = 0;
        double current_sell_price = 0;
        double current_buy_qty = 0;
        double current_sell_qty = 0;
        std::string buy_order_id = "";
        std::string sell_order_id = "";
        bool wait_buy_order_id = false;
        bool wait_sell_order_id = false;
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
            ws.send(msg);
        };

        auto recv_msg = [&ws](rapidjson::Document& d)
        {
            d.Parse(ws.recv().c_str());
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


                        bids.apply_changes(data["bids"]);
                        asks.apply_changes(data["asks"]);


                        if (isnan(position_usd))
                        {
                            continue;
                        }

                        // ---------------------------------------------------
                        // BUY ORDERS
                        // ---------------------------------------------------

                        if (buy_order_id.empty())
                        {
                            double buy_price = bids.price_depth(MID_DEPTH_QTY);

                            if ((position_usd < MAX_POSITION_USD) &&
                                (buy_price > 0) &&
                                (!wait_buy_order_id))
                            {
                                double buy_qty = CLIP(
                                    10 * floor(ORDER_AMOUNT * (1 - position_usd / MAX_POSITION_USD) / 10),
                                    0, 2 * ORDER_AMOUNT
                                );

                                
                                std::string label = "buy_" + instrument_name;

                                std::cout << "Sending order to buy at " << buy_price << std::endl;

                                send_msg("private/buy", {
                                        {"instrument_name", instrument_name},
                                        {"amount", buy_qty },
                                        {"type", "limit"},
                                        {"label", label},
                                        {"price", buy_price },
                                        {"post_only", "true"}
                                    });

                                current_buy_price = buy_price;
                                current_buy_qty = buy_qty;
                                wait_buy_order_id = true;
                            }
                        }
                        else
                        {
                            double buy_price = bids.price_depth(MID_DEPTH_QTY, current_buy_price, current_buy_qty);
                            double min_buy_price = bids.price_depth(MAX_DEPTH_QTY, current_buy_price, current_buy_qty);
                            double max_buy_price = bids.price_depth(MIN_DEPTH_QTY, current_buy_price, current_buy_qty);
                            //std::cout << "BIDS:\t" << min_buy_price << "\t" << buy_price << "\t" << max_buy_price << std::endl;

                            if ((current_buy_price > max_buy_price) || (current_buy_price < min_buy_price))
                            {
                                double buy_qty = CLIP(
                                    10 * floor(ORDER_AMOUNT * (1 - position_usd / MAX_POSITION_USD) / 10),
                                    0, 2 * ORDER_AMOUNT
                                );

                                //std::cout << "Sending request to edit buy order to " << buy_price << std::endl;

                                send_msg("private/edit", {
                                        {"order_id", buy_order_id },
                                        {"amount", buy_qty},
                                        {"price", buy_price}
                                    });

                                current_buy_price = buy_price;
                                current_buy_qty = buy_qty;
                            }
                        }

                        // ---------------------------------------------------
                        // SELL ORDERS
                        // ---------------------------------------------------

                        if (sell_order_id.empty())
                        {
                            double sell_price = asks.price_depth(MID_DEPTH_QTY);

                            if ((position_usd > -MAX_POSITION_USD) &&
                                (sell_price > 0) &&
                                (!wait_sell_order_id))
                            {
                                double sell_qty = CLIP(
                                    10 * floor(ORDER_AMOUNT * (1 + position_usd / MAX_POSITION_USD) / 10),
                                    0, 2 * ORDER_AMOUNT
                                );


                                std::string label = "sell_" + instrument_name;

                                std::cout << "Sending order to sell at " << sell_price << std::endl;

                                send_msg("private/sell", {
                                        {"instrument_name", instrument_name},
                                        {"amount", sell_qty },
                                        {"type", "limit"},
                                        {"label", label},
                                        {"price", sell_price },
                                        {"post_only", "true"}
                                    });

                                current_sell_price = sell_price;
                                current_sell_qty = sell_qty;
                                wait_sell_order_id = true;
                            }
                        }
                        else
                        {
                            double sell_price = asks.price_depth(MID_DEPTH_QTY, current_sell_price, current_sell_qty);
                            double max_sell_price = asks.price_depth(MAX_DEPTH_QTY, current_sell_price, current_sell_qty);
                            double min_sell_price = asks.price_depth(MIN_DEPTH_QTY * 0.5, current_sell_price, current_sell_qty);
                            //std::cout << "ASKS:\t" << min_sell_price << "\t" << sell_price << "\t" << max_sell_price << std::endl;

                            if ((current_sell_price > max_sell_price) || (current_sell_price < min_sell_price))
                            {
                                double sell_qty = CLIP(
                                    10 * floor(ORDER_AMOUNT * (1 + position_usd / MAX_POSITION_USD) / 10),
                                    0, 2 * ORDER_AMOUNT
                                );

                                // std::cout << "Sending request to edit sell order to " << sell_price << std::endl;

                                send_msg("private/edit", {
                                        {"order_id", sell_order_id },
                                        {"amount", sell_qty},
                                        {"price", sell_price}
                                    });

                                current_sell_price = sell_price;
                                current_sell_qty = sell_qty;
                            }
                        }
                    }
                    // -------------------------------------------------------
                    // Changes updates
                    // -------------------------------------------------------
                    else if (channel == changes_channel)
                    {
                        const auto& trades = data["trades"];

                        for (auto it = trades.Begin(); it != trades.End(); ++it)
                        {
                            const auto& trade = *it;
                            const std::string& direction = trade["direction"].GetString();
                            const std::string& state = trade["state"].GetString();
                            const double& amount = trade["amount"].GetDouble();

                            if (state == "filled")
                            {
                                if (direction == "buy")
                                {
                                    std::cout << "Suy order filled" << std::endl;
                                    position_usd += amount;
                                    buy_order_id = "";
                                    wait_buy_order_id = false;
                                }
                                else if (direction == "sell")
                                {
                                    std::cout << "Sell order filled" << std::endl;
                                    position_usd -= amount;
                                    sell_order_id = "";
                                    wait_sell_order_id = false;
                                }
                                else
                                {
                                    throw std::exception("Invalid direction.");
                                }
                            }
                            else if (state == "open")
                            {
                                if (direction == "buy")
                                {
                                    position_usd += amount;
                                }
                                else if (direction == "sell")
                                {
                                    position_usd -= amount;
                                }
                                else
                                {
                                    throw std::exception("Invalid direction.");
                                }
                            }
                            else
                            {
                                throw std::exception("Unknown state.");
                            }
                        }
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
                // private / edit
                // -----------------------------------------------------------

                if (method == "private/edit")
                {
                    const auto& order = result["order"];
                    const std::string& direction = order["direction"].GetString();
                    const std::string& order_id = order["order_id"].GetString();
                    
                    if (direction == "buy")
                    {
                        assert(order_id == buy_order_id);
                        // std::cout << "Received edit buy order confirmation" << std::endl;
                    }
                    else if (direction == "sell")
                    {
                        assert(order_id == sell_order_id);
                        // std::cout << "Received edit sell order confirmation" << std::endl;
                    }
                    else
                    {
                        throw std::exception("Invalid direction.");
                    }
                }

                // -----------------------------------------------------------
                // private / buy
                // -----------------------------------------------------------

                else if (method == "private/buy")
                {
                    const auto& order = result["order"];
                    buy_order_id = order["order_id"].GetString();
                    wait_buy_order_id = false;
                    std::cout << "Received buy order confirmation." << std::endl;
                }

                // -----------------------------------------------------------
                // private / sell
                // -----------------------------------------------------------

                else if (method == "private/sell")
                {
                    const auto& order = result["order"];
                    sell_order_id = order["order_id"].GetString();
                    wait_buy_order_id = false;
                    std::cout << "Received sell order confirmation." << std::endl;
                }

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
                const auto& it = prev_requests.find(response["id"].GetString());
                request = std::move(it->second);
                prev_requests.erase(it);

                const auto& error = response["error"];
                const auto& msg = error["message"].GetString();
                int code = error["code"].GetInt();

                std::cout << "Received error message: (" << code << ") " << msg << std::endl;

                if (code == 13009)
                {
                    std::cout << "Expired access_token, requesting a new one." << std::endl;
                    send_msg("public_auth", {
                            {"grant_type", "refresh_token"},
                            {"refresh_token", refresh_token}
                        });
                }
                else if ((code == 11044) || (code == 10010))
                {
                    // 11044 - Not open order
                    // 10010 - Already closed

                    const auto& params = (*request.get())["params"];
                    const std::string& order_id = params["order_id"].GetString();
                    const auto& amount = params["amount"].GetDouble();

                    if (order_id == buy_order_id)
                    {
                        buy_order_id = "";
                        wait_buy_order_id = false;
                        // position_usd += amount;
                        std::cout << "Closed buy order. Position=" << position_usd << std::endl;
                    }
                    else if (order_id == sell_order_id)
                    {
                        sell_order_id = "";
                        wait_sell_order_id = false;
                        // position_usd -= amount;
                        std::cout << "Closed sell order. Position=" << position_usd << std::endl;
                    }
                }
                else if (code == 13777)
                {
                    // ignore this error
                }
                else
                {
                    throw std::exception("Unexpected error");
                }
            }
        }
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}