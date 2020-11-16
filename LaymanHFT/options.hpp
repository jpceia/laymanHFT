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
	}
}
