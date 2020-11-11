#include <cstdlib>
#include <iostream>

#include <chrono>

#include <assert.h>
#include <math.h>
#include <ctime>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>


#include "deribit_session.hpp"
#include "book.hpp"

#define CLIP(x, a, b) x > a ? (x < b ? x : b) : a

namespace chrono = std::chrono;

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




struct API_Settings
{
    URI uri;
    std::string client_id;
    std::string client_secret;

};


class SimpleMM_Strategy : public DeribitSession
{
private:
    std::string _instrument;
    std::string _refresh_token;
    std::string _access_token;

    long prev_change_id;
    double position_usd;
    double current_buy_price;
    double current_sell_price;
    double current_buy_qty;
    double current_sell_qty;
    std::string buy_order_id;
    std::string sell_order_id;
    std::string book_channel, changes_channel;
    bool wait_buy_order_id, wait_sell_order_id;
    Bids bids;
    Asks asks;

public:
    //using DeribitSession::run;
    SimpleMM_Strategy(
        const API_Settings& settings,
        const std::string& instrument_name
    ) :
        DeribitSession(settings.uri),
        _instrument(instrument_name)
    {
        prev_change_id = 0; // book
        position_usd = NAN; // position class
        current_buy_price = 0; // buy order
        current_sell_price = 0;
        current_buy_qty = 0; // buy order
        current_sell_qty = 0;
        buy_order_id = ""; // buy order
        sell_order_id = "";
        wait_buy_order_id = false; // buy order
        wait_sell_order_id = false;
        
        book_channel = "book." + _instrument + "." + interval;
        changes_channel = "user.changes." + _instrument + "." + interval;

        if (!settings.client_id.empty())
        {
            this->send("public/auth", {
                    {"grant_type", "client_credentials"},
                    {"client_id", settings.client_id},
                    {"client_secret", settings.client_secret}
                });

            this->send("private/get_position", { {"instrument_name", instrument_name } });
        }

        // Requesting time from the API platform
        this->send("public/get_time");

        // setting heartbeat to check life
        this->send("public/set_heartbeat", { {"interval", "10"} });

        // subscribing channel with book information
        this->subscribe({ book_channel, changes_channel });
    }

    void on_notification(
        const std::string& method,
        const rapidjson::Value& params
    )
    {
        if (method == "subscription")
        {
            const std::string& channel = params["channel"].GetString();
            auto& data = params["data"];

            this->on_subscription_notification(channel, data);
        }
        else if (method == "heartbeat")
        {
            const std::string& type = params["type"].GetString();
            if (type == "test_request")
            {
                this->send("public/test");
                this->send("public/get_time");
            }
        }
        else
        {
            // others...
        }
    }

    void on_subscription_notification(
        const std::string& channel,
        const rapidjson::Value& data
    )
    {
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
                return;
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


                    std::string label = "buy_" + _instrument;

                    std::cout << "Sending order to buy at " << buy_price << std::endl;

                    this->send("private/buy", {
                            {"instrument_name", _instrument},
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

                    this->send("private/edit", {
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


                    std::string label = "sell_" + _instrument;

                    std::cout << "Sending order to sell at " << sell_price << std::endl;

                    this->send("private/sell", {
                            {"instrument_name", _instrument},
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

                    this->send("private/edit", {
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
                        std::cout << "Buy order filled" << std::endl;
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
                    throw std::exception("Unexpected state.");
                }
            }
        }
    }

    void on_response(
        const std::string& method,
        const rapidjson::Value& request,
        const rapidjson::Value& result
    )
    {

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
            assert(result["instrument_name"].GetString() == _instrument);
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
            _refresh_token = result["refresh_token"].GetString();
            _access_token = result["access_token"].GetString();
        }
    }

    void on_error(
        const std::string& method,
        const rapidjson::Value& request,
        int code,
        const std::string& msg
    )
    {
        std::cout << "Received error message: (" << code << ") " << msg << std::endl;

        if (code == 13009)
        {
            std::cout << "Expired access_token, requesting a new one." << std::endl;
            this->send("public/auth", {
                    {"grant_type", "refresh_token"},
                    {"refresh_token", _refresh_token}
                });
        }
        else if ((code == 11044) || (code == 10010))
        {
            // 11044 - Not open order
            // 10010 - Already closed
            const std::string& order_id = request["order_id"].GetString();
            const auto& amount = request["amount"].GetDouble();

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
};


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
        API_Settings set{ uri, client_id, client_secret };
        std::make_shared<SimpleMM_Strategy>(set, instrument_name)->run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}