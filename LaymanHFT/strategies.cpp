#include "strategies.hpp"

#include <boost/algorithm/clamp.hpp>
#include <iostream>
#include <chrono>


namespace chrono = std::chrono;


SimpleMM::SimpleMM(
    const API_Settings& settings,
    const Strategy_Params& params
) :
    DeribitSession(settings),
    kMinDepth(params.min_depth),
    kMidDepth(params.mid_depth),
    kMaxDepth(params.max_depth),
    kOrderAmount(params.order_amount),
    kMaxPositionUSD(params.max_position_usd),
    _instrument(params.instrument),
    _book_channel("book." + _instrument + "." + params.frequency),
    _changes_channel("user.changes." + _instrument + "." + params.frequency)
{
    _position_usd = NAN;

    this->send("private/get_position", { {"instrument_name", _instrument } });

    // Requesting time from the API platform
    this->send("public/get_time");

    // setting heartbeat to check life
    this->send("public/set_heartbeat", { {"interval", "10"} });

    // subscribing channel with book information
    this->subscribe({ _book_channel, _changes_channel });
}

void SimpleMM::on_notification(
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

void SimpleMM::on_subscription_notification(
    const std::string& channel,
    const rapidjson::Value& data
)
{
    // -------------------------------------------------------
    // Book updates
    // -------------------------------------------------------
    if (channel == _book_channel)
    {
        book.update(data);

        if (std::isnan(_position_usd))
        {
            return;
        }

        // ---------------------------------------------------
        // BUY ORDERS
        // ---------------------------------------------------

        if (buy_order.id.empty())
        {
            double buy_price = book.bids.price_depth(kMidDepth);

            if ((_position_usd < kMaxPositionUSD) &&
                (buy_price > 0) &&
                (!buy_order.wait))
            {
                double buy_qty = boost::algorithm::clamp(
                    10 * floor(kOrderAmount * (1 - _position_usd / kMaxPositionUSD) / 10),
                    0, 2 * kOrderAmount
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

                buy_order.price = buy_price;
                buy_order.quantity = buy_qty;
                buy_order.wait = true;
            }
        }
        else
        {
            double buy_price = book.bids.price_depth(kMidDepth, buy_order.price, buy_order.quantity);
            double min_buy_price = book.bids.price_depth(kMaxDepth, buy_order.price, buy_order.quantity);
            double max_buy_price = book.bids.price_depth(kMinDepth, buy_order.price, buy_order.quantity);

            if ((buy_order.price > max_buy_price) || (buy_order.price < min_buy_price))
            {
                double buy_qty = boost::algorithm::clamp(
                    10 * floor(kOrderAmount * (1 - _position_usd / kMaxPositionUSD) / 10),
                    0, 2 * kOrderAmount
                );

                //std::cout << "Sending request to edit buy order to " << buy_price << std::endl;

                this->send("private/edit", {
                        {"order_id", buy_order.id },
                        {"amount", buy_qty},
                        {"price", buy_price}
                    });

                buy_order.price = buy_price;
                buy_order.quantity = buy_qty;
            }
        }

        // ---------------------------------------------------
        // SELL ORDERS
        // ---------------------------------------------------

        if (sell_order.id.empty())
        {
            double sell_price = book.asks.price_depth(kMidDepth);

            if ((_position_usd > -kMaxPositionUSD) &&
                (sell_price > 0) &&
                (!sell_order.wait))
            {
                double sell_qty = boost::algorithm::clamp(
                    10 * floor(kOrderAmount * (1 + _position_usd / kMaxPositionUSD) / 10),
                    0, 2 * kOrderAmount
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

                sell_order.price = sell_price;
                sell_order.quantity = sell_qty;
                sell_order.wait = true;
            }
        }
        else
        {
            double sell_price = book.asks.price_depth(kMidDepth, sell_order.price, sell_order.quantity);
            double max_sell_price = book.asks.price_depth(kMaxDepth, sell_order.price, sell_order.quantity);
            double min_sell_price = book.asks.price_depth(kMinDepth, sell_order.price, sell_order.quantity);

            if ((sell_order.price > max_sell_price) || (sell_order.price < min_sell_price))
            {
                double sell_qty = boost::algorithm::clamp(
                    10 * floor(kOrderAmount * (1 + _position_usd / kMaxPositionUSD) / 10),
                    0, 2 * kOrderAmount
                );

                // std::cout << "Sending request to edit sell order to " << sell_price << std::endl;

                this->send("private/edit", {
                        {"order_id", sell_order.id },
                        {"amount", sell_qty},
                        {"price", sell_price}
                    });

                sell_order.price = sell_price;
                sell_order.quantity = sell_qty;
            }
        }
    }
    // -------------------------------------------------------
    // Changes updates
    // -------------------------------------------------------
    else if (channel == _changes_channel)
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
                    std::cout << trade << std::endl;
                    _position_usd += amount;
                    buy_order.id = "";
                    buy_order.wait = false;
                }
                else if (direction == "sell")
                {
                    std::cout << "Sell order filled" << std::endl;
                    std::cout << trade << std::endl;
                    _position_usd -= amount;
                    sell_order.id = "";
                    sell_order.wait = false;
                }
                else
                {
                    throw std::runtime_error("Invalid direction.");
                }
            }
            else if (state == "open")
            {
                if (direction == "buy")
                {
                    std::cout << "Buy order partially filled" << std::endl;
                    std::cout << trade << std::endl;
                    _position_usd += amount;
                }
                else if (direction == "sell")
                {
                    std::cout << "Sell order partially filled" << std::endl;
                    std::cout << trade << std::endl;
                    _position_usd -= amount;
                }
                else
                {
                    throw std::runtime_error("Invalid direction.");
                }
            }
            else
            {
                throw std::runtime_error("Unexpected state.");
            }
        }
    }
}

void SimpleMM::on_response(
    const std::string& method,
    const rapidjson::Value&, //request,
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
            assert(order_id == buy_order.id);
            // std::cout << "Received edit buy order confirmation" << std::endl;
        }
        else if (direction == "sell")
        {
            assert(order_id == sell_order.id);
            // std::cout << "Received edit sell order confirmation" << std::endl;
        }
        else
        {
            throw std::runtime_error("Invalid direction.");
        }
    }

    // -----------------------------------------------------------
    // private / buy
    // -----------------------------------------------------------

    else if (method == "private/buy")
    {
        const auto& order = result["order"];
        buy_order.id = order["order_id"].GetString();
        buy_order.wait = false;
        std::cout << "Received buy order confirmation." << std::endl;
    }

    // -----------------------------------------------------------
    // private / sell
    // -----------------------------------------------------------

    else if (method == "private/sell")
    {
        const auto& order = result["order"];
        sell_order.id = order["order_id"].GetString();
        sell_order.wait = false;
        std::cout << "Received sell order confirmation." << std::endl;
    }

    // -----------------------------------------------------------
    // private / get_position
    // -----------------------------------------------------------

    else if (method == "private/get_position")
    {
        assert(result["instrument_name"].GetString() == _instrument);
        const double& server_position_usd = result["size"].GetDouble();
        if (std::isnan(_position_usd))
        {
            _position_usd = server_position_usd;
            std::cout << "Initial position: " << _position_usd << std::endl;
        }
        else if (_position_usd != server_position_usd)
        {
            throw std::runtime_error("Position (USD) mismatch");
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
}

void SimpleMM::on_error(
    const std::string&, // method
    const rapidjson::Value& request,
    int code,
    const std::string& msg
)
{
    if ((code == 11044) || (code == 10010))
    {
        std::cout << "Received error message: (" << code << ") " << msg << std::endl;
        // 11044 - Not open order
        // 10010 - Already closed
        const std::string& order_id = request["order_id"].GetString();
        // const auto& amount = request["amount"].GetDouble();

        if (order_id == buy_order.id)
        {
            buy_order.id = "";
            buy_order.wait = false;
            // _position_usd += amount;
            std::cout << "Closed buy order. Position=" << _position_usd << std::endl;
        }
        else if (order_id == sell_order.id)
        {
            sell_order.id = "";
            sell_order.wait = false;
            // _position_usd -= amount;
            std::cout << "Closed sell order. Position=" << _position_usd << std::endl;
        }
    }
    else if (code == 13777)
    {
        // ignore this error
    }
    else
    {
        std::cout << "Received error message: (" << code << ") " << msg << std::endl;
        std::cout << request << std::endl;
        throw std::runtime_error("Unexpected error");
    }
}
