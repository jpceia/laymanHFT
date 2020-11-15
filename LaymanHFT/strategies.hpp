#pragma once
#include "deribit_session.hpp"
#include "book.hpp"


struct Strategy_Params
{
    double min_depth = 0;
    double mid_depth = 0;
    double max_depth = 0;
    double order_amount = 0;
    double max_position_usd = 0;
    std::string frequency = "raw";
    std::string instrument = "";
};



class SimpleMM : public DeribitSession
{
private:
    const double kMinDepth, kMidDepth, kMaxDepth;
    const double kOrderAmount, kMaxPositionUSD;
    std::string _instrument;
    std::string _book_channel, _changes_channel;
    double _position_usd;
    Order buy_order, sell_order;
    Book book;

public:
    //using DeribitSession::run;
    SimpleMM(
        const API_Settings&,    // Settings
        const Strategy_Params&  // Params
    );

    void on_notification(
        const std::string&,     // method
        const rapidjson::Value& // params
    );

    void on_subscription_notification(
        const std::string&,     // channel
        const rapidjson::Value& // content
    );

    void on_response(
        const std::string&,     // method
        const rapidjson::Value&,// request
        const rapidjson::Value& // response
    );

    void on_error(
        const std::string&,     // mehtod
        const rapidjson::Value&,// request
        int,                    // error code
        const std::string&      // error message
    );
};