#include "deribit_session.hpp"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>


DeribitSession::DeribitSession(const URI& uri)
    : _session(uri)
{}

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
    rapidjson::Document d;
    while (_session.is_open())
    {
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

        on_response(method, params, result);
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

        on_error(method, params, code, msg);
    }

}