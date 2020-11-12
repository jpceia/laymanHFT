#pragma once
#include "connection.hpp"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/variant.hpp>

#include <rapidjson/document.h>

#include <unordered_map>
#include <string>

namespace uuids = boost::uuids;         // from <boost/uuid/uuid.hpp>


struct API_Settings
{
    URI uri;
    std::string client_id;
    std::string client_secret;
};


class DeribitSession
{
private:
    WSSession _session;
    uuids::random_generator _uuid_gen;
    std::unordered_map<std::string, std::pair<std::string, rapidjson::Document>> _requests;
    std::string _refresh_token;
    std::string _access_token;

    void send_json(const std::string&, rapidjson::Document&);

public:
    DeribitSession(const API_Settings&);

    void send(const std::string&);

    void send(const std::string&, const std::map < std::string, boost::variant<std::string, double>>&);

    void subscribe(const std::vector<std::string>& channels);

    void run();

    virtual void on_message(const rapidjson::Value& message);

    virtual void on_notification(
        const std::string&,         // method
        const rapidjson::Value&     // params (content)
    ) = 0;

    virtual void on_response(
        const std::string&, //method
        const rapidjson::Value&, // request message
        const rapidjson::Value&  // response contents
    ) = 0;

    virtual void on_error(
        const std::string&,         // method
        const rapidjson::Value&,    // request message
        int,                        // error code
        const std::string&          // error message
    ) = 0;
};

