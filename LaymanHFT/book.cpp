#include "book.hpp"
#include <string>
#include <iostream>

// explicit instantiations
template class GenericBook<std::less<double> >;
template class GenericBook<std::greater<double> >;


template<typename Compare>
GenericBook<Compare>::GenericBook() :
	m_data(new std::map<double, double, Compare>())
{
}

template<typename Compare>
GenericBook<Compare>::~GenericBook()
{
	delete m_data;
}

template<typename Compare>
void GenericBook<Compare>::apply_changes(rapidjson::Value& arr)
{
	assert(arr.IsArray());

	for (auto it = arr.Begin(); it != arr.End(); ++it)
	{
		const std::string& action = (*it)[0].GetString();
		const double& price = (*it)[1].GetDouble();
		const double& quantity = (*it)[2].GetDouble();

		if (action == "new")
		{
			m_data->insert({ price, quantity });
		}
		else if (action == "change")
		{
			auto it = m_data->find(price);
			assert(it != m_data->end());
			it->second = quantity;
		}
		else if (action == "delete")
		{
			auto it = m_data->find(price);
			assert(it != m_data->end());
			m_data->erase(it);
		}
		else
		{
			throw std::exception("Invalid change type");
		}
	}
}

template<typename Compare>
void GenericBook<Compare>::print()
{
	std::cout << std::endl;
	std::cout << "Price\tQuantity" << std::endl;
	for (auto it : *m_data)
	{
		std::cout << it.first << "\t" << it.second << std::endl;
	}
}

template<typename Compare>
double GenericBook<Compare>::price_depth(double quantity)
{
	double cum_qty = 0;
	double price = -1;

	for (const auto& it : *m_data)
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