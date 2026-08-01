#include <exception>
#include <iostream>

#include "app_config.hpp"
#include "market_maker_app.hpp"

#ifndef DEFAULT_SIDE_MODE
#define DEFAULT_SIDE_MODE "both"
#endif

int main(int argc, char **argv)
{
    try
    {
        return run_market_maker(load_config(argc, argv, DEFAULT_SIDE_MODE));
    }
    catch (const std::exception &error)
    {
        std::cerr << "Startup failed: " << error.what() << '\n';
        return 1;
    }
}
