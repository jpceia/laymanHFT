#include "book.hpp"
#include <string>
#include <iostream>

// explicit instantiations
template class BookSide<std::less<double> >;
template class BookSide<std::greater<double> >;


template<typename Compare>
BookSide<Compare>::BookSide() :
	_data(new std::map<double, double, Compare>())
{
}

template<typename Compare>
BookSide<Compare>::~BookSide()
{
	delete _data;
}

template<typename Compare>
void BookSide<Compare>::update(const rapidjson::Value& arr)
{
	assert(arr.IsArray());

	for (auto it = arr.Begin(); it != arr.End(); ++it)
	{
		const std::string& action = (*it)[0].GetString();
		const double& price = (*it)[1].GetDouble();
		const double& quantity = (*it)[2].GetDouble();

		if (action == "new")
		{
			_data->insert({ price, quantity });
		}
		else if (action == "change")
		{
			auto it = _data->find(price);
			assert(it != _data->end());
			it->second = quantity;
		}
		else if (action == "delete")
		{
			auto it = _data->find(price);
			assert(it != _data->end());
			_data->erase(it);
		}
		else
		{
			throw std::exception("Invalid change type");
		}
	}
}

template<typename Compare>
void BookSide<Compare>::print()
{
	std::cout << std::endl;
	std::cout << "Price\tQuantity" << std::endl;
	for (auto it : *_data)
	{
		std::cout << it.first << "\t" << it.second << std::endl;
	}
}

template<typename Compare>
double BookSide<Compare>::price_depth(
	double quantity,
	double order_price,
	double order_quantity)
{
	double cum_qty = 0;
	double price = -1;

	for (const auto& it : *_data)
	{
		cum_qty += it.second;
		if (order_price == it.first)
		{
			cum_qty -= order_quantity;
		}

		if (cum_qty > quantity)
		{
			price = it.first;
			break;
		}
	}

	return price;
}

template<typename Compare>
double BookSide<Compare>::price_depth(double quantity)
{
	double cum_qty = 0;
	double price = -1;

	for (const auto& it : *_data)
	{
		cum_qty += it.second;

		if (cum_qty > quantity)
		{
			price = it.first;
			break;
		}
	}

	return price;
}


void Book::update(const rapidjson::Value& data)
{
	long change_id = data["change_id"].GetInt64();
	if (data.HasMember("prev_change_id"))
	{
		long prev_change_id = data["prev_change_id"].GetInt64();
		if (_prev_change_id != prev_change_id)
		{
			throw std::exception("Invalid change_id sequence");
		}
	}
	_prev_change_id = change_id;

	this->bids.update(data["bids"]);
	this->asks.update(data["asks"]);
}