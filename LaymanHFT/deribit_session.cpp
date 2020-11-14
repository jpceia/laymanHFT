#include "deribit_session.hpp"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>


std::ostream& operator<<(std::ostream& os, const rapidjson::Value& d)
{
    rapidjson::StringBuffer buffer;
    // buffer.Clear();
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    d.Accept(writer);
    return os << std::string(buffer.GetString(), buffer.GetSize());
}


DeribitSession::DeribitSession(const API_Settings& settings)
    : _session(settings.uri)
{
    if (!settings.client_id.empty())
    {
        this->send("public/auth", {
                {"grant_type", "client_credentials"},
                {"client_id", settings.client_id},
                {"client_secret", settings.client_secret}
            });
    }
}

void DeribitSession::send_json(const std::string& method, rapidjson::Document& params)
{
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& alloc = d.GetAllocator();
    d.SetObject();

    std::string id = uuids::to_string(_uuid_gen());

    d.SetObject();
    d.AddMember("jsonrc", "2.0", alloc);
    d.AddMember("id", rapidjson::Value(id.c_str(), alloc), alloc);
    d.AddMember("method", rapidjson::Value(method.c_str(), alloc), alloc);
    d.AddMember("params", params, params.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    d.Accept(writer);
    const std::string& msg = std::string(buffer.GetString(), buffer.GetSize());
    _session.send(msg);

    rapidjson::Document doc;
    doc.CopyFrom(d["params"], doc.GetAllocator());
    _requests.insert({id, std::make_pair(method, std::move(doc))});
}

void DeribitSession::send(const std::string& method)
{
    rapidjson::Document d;
    d.SetObject();
    this->send_json(method, d);
}

void DeribitSession::send(const std::string& method, const std::map < std::string, boost::variant<std::string, double>>& params)
{
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& alloc = d.GetAllocator();
    d.SetObject();

    for (auto& it : params)
    {
        if (it.second.which() == 0) // string
        {
            d.AddMember(
                rapidjson::Value(it.first.c_str(), alloc),
                rapidjson::Value(boost::get<std::string>(it.second).c_str(), alloc),
                alloc);
        }
        else if (it.second.which() == 1) // double
        {
            d.AddMember(
                rapidjson::Value(it.first.c_str(), alloc),
                rapidjson::Value(boost::get<double>(it.second)),
                alloc);
        }
    }

    this->send_json(method, d);
}

void DeribitSession::subscribe(const std::vector<std::string>& channels)
{
    rapidjson::Document d;
    rapidjson::Document::AllocatorType& alloc = d.GetAllocator();
    d.SetObject();

    rapidjson::Value channels_json(rapidjson::kArrayType);

    for (auto& channel : channels)
    {
        channels_json.PushBack(rapidjson::Value(channel.c_str(), alloc), alloc);
    }

    d.AddMember("channels", channels_json, alloc);

    this->send_json("public/subscribe", d);
}

void DeribitSession::run()
{
    while (_session.is_open())
    {
        rapidjson::Document d;
        d.Parse(_session.recv().c_str());
        this->on_message(d);
    }
}

void DeribitSession::on_message(const rapidjson::Value& message) {

    if (message.HasMember("method") && message["method"].IsString()) // Notification
    {
        const std::string& method = message["method"].GetString();
        auto& params = message["params"];
        on_notification(method, params);
    }
    else if (message.HasMember("result"))
    {
        const auto& it = _requests.find(message["id"].GetString());
        auto request = std::move(it->second);
        _requests.erase(it);

        const std::string& method = request.first;
        const auto& params = request.second;
        const auto& result = message["result"];

        // -----------------------------------------------------------
        // public / auth
        // -----------------------------------------------------------

        if (method == "public/auth")
        {
            // std::cout << "Receiving new authorization tokens." << std::endl;
            _refresh_token = result["refresh_token"].GetString();
            _access_token = result["access_token"].GetString();
        }
        else
        {
            on_response(method, params, result);
        }
    }
    else if (message.HasMember("error"))
    {
        const auto& it = _requests.find(message["id"].GetString());
        auto request = std::move(it->second);
        _requests.erase(it);

        const std::string& method = request.first;
        const auto& params = request.second;
        const auto& error = message["error"];
        int code = error["code"].GetInt();
        const auto& msg = error["message"].GetString();

        if (code == 13009)
        {
            // std::cout << "Expired access_token, requesting a new one." << std::endl;
            this->send("public/auth", {
                    {"grant_type", "refresh_token"},
                    {"refresh_token", _refresh_token}
                });
        }
        else
        {
            on_error(method, params, code, msg);
        }
    }
}


SubscriptionWriter::SubscriptionWriter(
    const URI& uri,
    const std::string& fname,
    const std::vector<std::string>& channels) :
    DeribitSession({ uri, "", "" })
{
    this->subscribe(channels);

    _ofile.open(fname);
}

SubscriptionWriter::~SubscriptionWriter()
{
    _ofile.close();
}

void SubscriptionWriter::on_notification(
    const std::string& method,
    const rapidjson::Value& params
)
{
    if (method != "subscription")
    {
        return;
    }

    _ofile << params["data"] << std::endl;
}