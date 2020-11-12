#pragma once

#include <map>
#include <memory>
#include <functional>
#include <rapidjson/document.h>


struct Order
{
	double price = 0;
	double quantity = 0;
	std::string id = "";
	bool wait = false;
};



template<typename Compare>
class BookSide
{
public:
	BookSide();
	~BookSide();

	void update(const rapidjson::Value&);
	void print();

	double price_depth(double, double, double);
	double price_depth(double);

private:
	std::map<double, double, Compare>* _data;
};


using Asks = BookSide<std::less<double>>;
using Bids = BookSide<std::greater<double>>;


class Book
{
public:
	Asks asks;
	Bids bids;

	void update(const rapidjson::Value&);

private:
	long _prev_change_id;
};