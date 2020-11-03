#pragma once

#include <map>
#include <memory>
#include <functional>
#include <rapidjson/document.h>

template<typename Compare>
class GenericBook
{
public:
	GenericBook();
	~GenericBook();

	void apply_changes(rapidjson::Value&);
	void print();

	double price_depth(double, double, double);
	double price_depth(double);

private:
	std::map<double, double, Compare>* m_data;
};


using Asks = GenericBook<std::less<double>>;
using Bids = GenericBook<std::greater<double>>;
