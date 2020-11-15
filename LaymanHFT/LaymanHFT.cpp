#include <iostream>
#include "strategies.hpp"
#include <boost/program_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/algorithm/string.hpp>

namespace po = boost::program_options;

int main(int argc, char** argv)
{

    API_Settings settings;
    Strategy_Params params;

    // Declare the supported options.
    po::options_description opts_desc("Allowed options");
    opts_desc.add_options()
        ("help",            "Produces help message")
        ("command",         po::value<std::string>()->required(),                                               "Command to run, either MM or Writer")
        ("live",            po::value<bool>()->default_value(false)->implicit_value(true),                      "Defines if we use the live or de test(default) platform")
        ("client_id",       po::value<std::string>(&settings.client_id)->default_value(     ""),                "Client ID")
        ("client_secret",   po::value<std::string>(&settings.client_secret)->default_value( ""),                "Client Secret")

        ("channels",        po::value<std::vector<std::string>>(),                                              "Channels to subscribe")
        ("output,o",        po::value<std::string>()->default_value(""),                                        "Output file")

        ("instrument",      po::value<std::string>(&params.instrument)->default_value(      "BTC-PERPETUAL"),   "Instrument to trade")
        ("min_depth",       po::value<double>(&params.min_depth),                                               "Minimum depth")
        ("mid_depth",       po::value<double>(&params.mid_depth),                                               "Mean depth")
        ("max_depth",       po::value<double>(&params.max_depth),                                               "Maximum depth")
        ("order_amount",    po::value<double>(&params.order_amount),                                            "Standard Order Amount")
        ("max_position_usd",po::value<double>(&params.max_position_usd),                                        "Maximum allowed position (in USD)")
        ;
    
    po::positional_options_description pos_opts_desc;
    pos_opts_desc.add("command", -1);

    po::variables_map opts_var_map;
    auto parsed = po::command_line_parser(argc, argv)
        .options(opts_desc)
        .positional(pos_opts_desc)
        .run();
    po::store(parsed, opts_var_map);

    if (opts_var_map.count("help")) {
        std::cout << opts_desc << "\n";
        return EXIT_FAILURE;
    }

    po::notify(opts_var_map);

    std::string command = boost::algorithm::to_lower_copy(opts_var_map["command"].as<std::string>());
    settings.uri = parseURI(opts_var_map["live"].as<bool>() ?
        "wss://www.deribit.com/ws/api/v2" :
        "wss://test.deribit.com/ws/api/v2"
    );

    if (command == "mm")
    {
        if (settings.client_id.empty())
        {
            std::cout << "client_id and client_secret need to be provided" << std::endl;
            return EXIT_FAILURE;
        }

        std::make_shared<SimpleMM>(settings, params)->run();
    }
    else if (command == "writer")
    {
        const std::string& fname = opts_var_map["output"].as<std::string>();
        const auto& channels = opts_var_map["channels"].as<std::vector<std::string>>();
        
        std::make_shared<SubscriptionWriter>(settings.uri, fname, channels)->run();
    }
    else
    {
        std::cout << "Invalid command. Valid commands are: mm, writer" << std::endl;
    }
    
    return EXIT_SUCCESS;
}
