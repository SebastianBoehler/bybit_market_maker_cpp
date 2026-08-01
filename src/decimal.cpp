#include "decimal.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
int precision_for_step(double step)
{
    if (!std::isfinite(step) || step <= 0.0)
        return 15;
    double scaled = step;
    for (int precision = 0; precision <= 15; ++precision)
    {
        if (std::abs(scaled - std::round(scaled)) <=
            1e-9 * std::max(1e-12, std::abs(scaled)))
            return precision;
        scaled *= 10.0;
    }
    return 15;
}
} // namespace

std::string format_decimal(double value, double step)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision_for_step(step)) << value;
    auto text = output.str();
    if (text.find('.') != std::string::npos)
    {
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
    }
    return text == "-0" ? "0" : text;
}
