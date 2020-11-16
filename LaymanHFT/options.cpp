#include "options.hpp"
#include <stdexcept>
#include <cmath>

#define INV_SQRT_2PI 0.3989422804014327
#define M_PI         3.14159265358979323846


double normal_cdf(double x) // Phi(-oo, x) aka N(x)
{
	// return std::erfc(-x / std::sqrt(2)) / 2;
	double k = 1.0 / (1.0 + 0.2316419 * x);
	double k_sum = k * (0.319381530 + k * (-0.356563782 + k * (1.781477937 + k * (-1.821255978 + 1.330274429 * k))));

	if (x >= 0.0)
	{
		return (1.0 - (1.0 / (pow(2 * M_PI, 0.5))) * exp(-0.5 * x * x) * k_sum);
	}
	else
	{
		return 1.0 - normal_cdf(-x);
	}
}

double normal_pdf(double x)
{
	return INV_SQRT_2PI * std::exp(-0.5 * x * x);
}


namespace options
{
	namespace black_scholes
	{

		double Premium(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t
		)
		{
			double time_to_maturity = difftime(option.maturity, t) / (24 * 60 * 60 * 365.0);  // conversion to years
			double sqrt_time_to_maturity = std::sqrt(time_to_maturity);
			double d1 =
				std::log(forward_price / option.strike) / (vol * sqrt_time_to_maturity) +
				0.5 * vol * sqrt_time_to_maturity;
			double d2 = d1 - vol * sqrt_time_to_maturity;
			double discount = spot_price / forward_price;
			double N1, N2, n;

			switch (option.type)
			{
			case OPTION_TYPE::CALL:
				N1 = normal_cdf(d1);
				N2 = normal_cdf(d2);
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::PUT:
				N1 = normal_cdf(d1) - 1;
				N2 = normal_cdf(d2) - 1;
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::STRADDLE:
				N1 = 2 * normal_cdf(d1) - 1;
				N2 = 2 * normal_cdf(d2) - 1;
				n = 2 * normal_pdf(d1);
				break;
			default:
				throw std::exception("Unknown option type");
			}

			return discount * (forward_price * N1 - option.strike * N2);
		}

		double Delta(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t
		)
		{
			double time_to_maturity = difftime(option.maturity, t) / (24 * 60 * 60 * 365.0);  // conversion to years
			double sqrt_time_to_maturity = std::sqrt(time_to_maturity);
			double d1 =
				std::log(forward_price / option.strike) / (vol * sqrt_time_to_maturity) +
				0.5 * vol * sqrt_time_to_maturity;
			double N1;

			switch (option.type)
			{
			case OPTION_TYPE::CALL:
				N1 = normal_cdf(d1);
				break;
			case OPTION_TYPE::PUT:
				N1 = normal_cdf(d1) - 1;
				break;
			case OPTION_TYPE::STRADDLE:
				N1 = 2 * normal_cdf(d1) - 1;
				break;
			default:
				throw std::exception("Unknown option type");
			}

			return N1;
		}


		double Gamma(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t
		)
		{
			double time_to_maturity = difftime(option.maturity, t) / (24 * 60 * 60 * 365.0);  // conversion to years
			double sqrt_time_to_maturity = std::sqrt(time_to_maturity);
			double d1 =
				std::log(forward_price / option.strike) / (vol * sqrt_time_to_maturity) +
				0.5 * vol * sqrt_time_to_maturity;
			double d2 = d1 - vol * sqrt_time_to_maturity;
			double discount = spot_price / forward_price;
			double n;

			switch (option.type)
			{
			case OPTION_TYPE::CALL:
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::PUT:
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::STRADDLE:
				n = 2 * normal_pdf(d1);
				break;
			default:
				throw std::exception("Unknown option type");
			}

			return n / (spot_price * vol * sqrt_time_to_maturity);
		}


		double Vega(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t
		)
		{
			double time_to_maturity = difftime(option.maturity, t) / (24 * 60 * 60 * 365.0);  // conversion to years
			double sqrt_time_to_maturity = std::sqrt(time_to_maturity);
			double d1 =
				std::log(forward_price / option.strike) / (vol * sqrt_time_to_maturity) +
				0.5 * vol * sqrt_time_to_maturity;
			double n;

			switch (option.type)
			{
			case OPTION_TYPE::CALL:
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::PUT:
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::STRADDLE:
				n = 2 * normal_pdf(d1);
				break;
			default:
				throw std::exception("Unknown option type");
			}

			return spot_price * sqrt_time_to_maturity * n;
		}


		void implied_vol_step(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t,

			// result variables
			double& premium,
			double& vega
		)
		{
			double time_to_maturity = difftime(option.maturity, t) / (24 * 60 * 60 * 365);  // conversion to years
			double sqrt_time_to_maturity = std::sqrt(time_to_maturity);
			double d1 =
				std::log(forward_price / option.strike) / (vol * sqrt_time_to_maturity) +
				0.5 * vol * sqrt_time_to_maturity;
			double d2 = d1 - vol * sqrt_time_to_maturity;
			double discount = spot_price / forward_price;
			double N1, N2, n;

			switch (option.type)
			{
			case OPTION_TYPE::CALL:
				N1 = normal_cdf(d1);
				N2 = normal_cdf(d2);
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::PUT:
				N1 = normal_cdf(d1) - 1;
				N2 = normal_cdf(d2) - 1;
				n = normal_pdf(d1);
				break;
			case OPTION_TYPE::STRADDLE:
				N1 = 2 * normal_cdf(d1) - 1;
				N2 = 2 * normal_cdf(d2) - 1;
				n = 2 * normal_pdf(d1);
				break;
			default:
				throw std::exception("Unknown option type");
			}

			premium = discount * (forward_price * N1 - option.strike * N2);
			vega = spot_price * sqrt_time_to_maturity * n;
		}


		double implied_vol(
			const Option& option,
			double market_premium,
			double spot_price,
			double forward_price,
			time_t t,
			double tol,
			size_t max_steps,
			double initial_guess
		)
		{
			double vol = initial_guess;
			double premium, vega;
			double diff;

			for (size_t k = 0; k < max_steps; k++)
			{
				implied_vol_step(
					option,
					spot_price,
					forward_price,
					vol,
					t,
					premium,
					vega
				);

				diff = market_premium - premium;
				vol += diff / vega;

				if (std::abs(diff) < tol)
				{
					return vol;
				}
			}

			throw std::runtime_error("Algorithm did not converge.");
		}
	}
}
