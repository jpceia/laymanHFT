#include <iostream>
#include "strategies.hpp"



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
        API_Settings settings{ uri, client_id, client_secret };

        Strategy_Params params;
        params.min_depth = 500;
        params.mid_depth = 2000;
        params.max_depth = 15000;
        params.order_amount = 5000;
        params.max_position_usd = 50000;
        params.instrument = instrument_name;

        std::make_shared<SimpleMM>(settings, params)->run();
    }
    catch (std::runtime_error const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
