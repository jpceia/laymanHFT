#pragma once
#include <ctime>



namespace options
{
	enum OPTION_TYPE
	{
		CALL = 0,
		PUT,
		STRADDLE
	};

	struct Option
	{
		OPTION_TYPE type;
		double strike;
		time_t maturity;
	};


	namespace black_scholes
	{
		double Premium(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t = time(0)
		);

		double Delta(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t = time(0)
		);

		double Gamma(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t = time(0)
		);

		double Vega(
			const Option& option,
			double spot_price,
			double forward_price,
			double vol,
			time_t t = time(0)
		);

		double implied_vol(
			const Option& option,
			double premium,
			double spot_price,
			double forward_price,
			time_t t = time(0),
			double tol = 1e-6,
			size_t max_steps = 100,
			double initial_guess = 1.0
		);
	}
}
