/********************************************************************************
 *                                                                              *
 * This file is part of IfcOpenShell.                                           *
 *                                                                              *
 * IfcOpenShell is free software: you can redistribute it and/or modify         *
 * it under the terms of the Lesser GNU General Public License as published by  *
 * the Free Software Foundation, either version 3.0 of the License, or          *
 * (at your option) any later version.                                          *
 *                                                                              *
 * IfcOpenShell is distributed in the hope that it will be useful,              *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of               *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
 * Lesser GNU General Public License for more details.                          *
 *                                                                              *
 * You should have received a copy of the Lesser GNU General Public License     *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.         *
 *                                                                              *
 ********************************************************************************/

#include "IfcWrite.h"

#include "IfcCharacterDecoder.h"

#include <boost/algorithm/string.hpp>
#include <iomanip>
#include <limits>
#include <locale>

using namespace IfcWrite;

class SizeVisitor : public boost::static_visitor<int> {
  public:
    int operator()(const boost::blank& /*i*/) const { return -1; }
    int operator()(const IfcWriteArgument::Derived& /*i*/) const { return -1; }
    int operator()(const int& /*i*/) const { return -1; }
    int operator()(const bool& /*i*/) const { return -1; }
    int operator()(const boost::logic::tribool& /*i*/) const { return -1; }
    int operator()(const double& /*i*/) const { return -1; }
    int operator()(const std::string& /*i*/) const { return -1; }
    int operator()(const boost::dynamic_bitset<>& /*i*/) const { return -1; }
    int operator()(const IfcWriteArgument::empty_aggregate_t&) const { return 0; }
    int operator()(const IfcWriteArgument::empty_aggregate_of_aggregate_t&) const { return 0; }
    int operator()(const std::vector<int>& i) const { return (int)i.size(); }
    int operator()(const std::vector<double>& i) const { return (int)i.size(); }
    int operator()(const std::vector<std::vector<int>>& i) const { return (int)i.size(); }
    int operator()(const std::vector<std::vector<double>>& i) const { return (int)i.size(); }
    int operator()(const std::vector<std::string>& i) const { return (int)i.size(); }
    int operator()(const std::vector<boost::dynamic_bitset<>>& i) const { return (int)i.size(); }
    int operator()(const IfcWriteArgument::EnumerationReference& /*i*/) const { return -1; }
    int operator()(const IfcUtil::IfcBaseClass* const& /*i*/) const { return -1; }
    int operator()(const aggregate_of_instance::ptr& i) const { return i->size(); }
    int operator()(const aggregate_of_aggregate_of_instance::ptr& i) const { return i->size(); }
};

class StringBuilderVisitor : public boost::static_visitor<void> {
  private:
    StringBuilderVisitor(const StringBuilderVisitor&);            //N/A
    StringBuilderVisitor& operator=(const StringBuilderVisitor&); //N/A

    std::ostringstream& data_;
    template <typename T>
    void serialize(const std::vector<T>& i) {
        data_ << "(";
        for (typename std::vector<T>::const_iterator it = i.begin(); it != i.end(); ++it) {
            if (it != i.begin()) {
                data_ << ",";
            }
            data_ << *it;
        }
        data_ << ")";
    }
    // The REAL token definition from the IFC SPF standard does not necessarily match
    // the output of the C++ ostream formatting operation.
    // REAL = [ SIGN ] DIGIT { DIGIT } "." { DIGIT } [ "E" [ SIGN ] DIGIT { DIGIT } ] .
    std::string format_double(const double& d) {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss << std::setprecision(std::numeric_limits<double>::digits10) << d;
        const std::string str = oss.str();
        oss.str("");
        std::string::size_type e = str.find('e');
        if (e == std::string::npos) {
            e = str.find('E');
        }
        const std::string mantissa = str.substr(0, e);
        oss << mantissa;
        if (mantissa.find('.') == std::string::npos) {
            oss << ".";
        }
        if (e != std::string::npos) {
            oss << "E";
            oss << str.substr(e + 1);
        }
        return oss.str();
    }

    std::string format_binary(const boost::dynamic_bitset<>& b) {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss.put('"');
        oss << std::uppercase << std::hex << std::setw(1);
        unsigned c = (unsigned)b.size();
        unsigned n = (4 - (c % 4)) & 3;
        oss << n;
        for (unsigned i = 0; i < c + n;) {
            unsigned accum = 0;
            for (int j = 0; j < 4; ++j, ++i) {
                unsigned bit = i < n ? 0 : b.test(c - i + n - 1) ? 1
                                                                 : 0;
                accum |= bit << (3 - j);
            }
            oss << accum;
        }
        oss.put('"');
        return oss.str();
    }

    bool upper_;

  public:
    StringBuilderVisitor(std::ostringstream& stream, bool upper = false)
        : data_(stream),
          upper_(upper) {}
    void operator()(const boost::blank& /*i*/) { data_ << "$"; }
    void operator()(const IfcWriteArgument::Derived& /*i*/) { data_ << "*"; }
    void operator()(const int& i) { data_ << i; }
    void operator()(const bool& i) { data_ << (i ? ".T." : ".F."); }
    void operator()(const boost::logic::tribool& i) { data_ << (i ? ".T." : (boost::logic::indeterminate(i) ? ".U." : ".F.")); }
    void operator()(const double& i) { data_ << format_double(i); }
    void operator()(const boost::dynamic_bitset<>& i) { data_ << format_binary(i); }
    void operator()(const std::string& i) {
        std::string s = i;
        if (upper_) {
            data_ << static_cast<std::string>(IfcCharacterEncoder(s));
        } else {
            data_ << '\'' << s << '\'';
        }
    }
    void operator()(const std::vector<int>& i);
    void operator()(const std::vector<double>& i);
    void operator()(const std::vector<std::string>& i);
    void operator()(const std::vector<boost::dynamic_bitset<>>& i);
    void operator()(const IfcWriteArgument::EnumerationReference& i) {
        data_ << "." << i.enumeration_value << ".";
    }
    void operator()(const IfcUtil::IfcBaseClass* const& i) {
        const IfcEntityInstanceData& e = i->data();
        if (e.type()->as_entity() == nullptr) {
            data_ << e.toString(upper_);
        } else {
            data_ << "#" << e.id();
        }
    }
    void operator()(const aggregate_of_instance::ptr& i) {
        data_ << "(";
        for (aggregate_of_instance::it it = i->begin(); it != i->end(); ++it) {
            if (it != i->begin()) {
                data_ << ",";
            }
            (*this)(*it);
        }
        data_ << ")";
    }
    void operator()(const std::vector<std::vector<int>>& i);
    void operator()(const std::vector<std::vector<double>>& i);
    void operator()(const aggregate_of_aggregate_of_instance::ptr& i) {
        data_ << "(";
        for (aggregate_of_aggregate_of_instance::outer_it outer_it = i->begin(); outer_it != i->end(); ++outer_it) {
            if (outer_it != i->begin()) {
                data_ << ",";
            }
            data_ << "(";
            for (aggregate_of_aggregate_of_instance::inner_it inner_it = outer_it->begin(); inner_it != outer_it->end(); ++inner_it) {
                if (inner_it != outer_it->begin()) {
                    data_ << ",";
                }
                (*this)(*inner_it);
            }
            data_ << ")";
        }
        data_ << ")";
    }
    void operator()(const IfcWriteArgument::empty_aggregate_t&) const { data_ << "()"; }
    void operator()(const IfcWriteArgument::empty_aggregate_of_aggregate_t&) const { data_ << "()"; }
    operator std::string() { return data_.str(); }
};

template <>
void StringBuilderVisitor::serialize(const std::vector<std::string>& i) {
    data_ << "(";
    for (std::vector<std::string>::const_iterator it = i.begin(); it != i.end(); ++it) {
        if (it != i.begin()) {
            data_ << ",";
        }
        std::string encoder = IfcCharacterEncoder(*it);
        data_ << encoder;
    }
    data_ << ")";
}

template <>
void StringBuilderVisitor::serialize(const std::vector<double>& i) {
    data_ << "(";
    for (std::vector<double>::const_iterator it = i.begin(); it != i.end(); ++it) {
        if (it != i.begin()) {
            data_ << ",";
        }
        data_ << format_double(*it);
    }
    data_ << ")";
}

template <>
void StringBuilderVisitor::serialize(const std::vector<boost::dynamic_bitset<>>& i) {
    data_ << "(";
    for (std::vector<boost::dynamic_bitset<>>::const_iterator it = i.begin(); it != i.end(); ++it) {
        if (it != i.begin()) {
            data_ << ",";
        }
        data_ << format_binary(*it);
    }
    data_ << ")";
}

void StringBuilderVisitor::operator()(const std::vector<int>& i) { serialize(i); }
void StringBuilderVisitor::operator()(const std::vector<double>& i) { serialize(i); }
void StringBuilderVisitor::operator()(const std::vector<std::string>& i) { serialize(i); }
void StringBuilderVisitor::operator()(const std::vector<boost::dynamic_bitset<>>& i) { serialize(i); }
void StringBuilderVisitor::operator()(const std::vector<std::vector<int>>& i) {
    data_ << "(";
    for (std::vector<std::vector<int>>::const_iterator it = i.begin(); it != i.end(); ++it) {
        if (it != i.begin()) {
            data_ << ",";
        }
        serialize(*it);
    }
    data_ << ")";
}
void StringBuilderVisitor::operator()(const std::vector<std::vector<double>>& i) {
    data_ << "(";
    for (std::vector<std::vector<double>>::const_iterator it = i.begin(); it != i.end(); ++it) {
        if (it != i.begin()) {
            data_ << ",";
        }
        serialize(*it);
    }
    data_ << ")";
}

IfcWriteArgument::operator int() const { return as<int>(); }
IfcWriteArgument::operator bool() const { return as<bool>(); }
IfcWriteArgument::operator boost::logic::tribool() const { return as<boost::logic::tribool>(); }
IfcWriteArgument::operator double() const { return as<double>(); }
IfcWriteArgument::operator std::string() const {
    if (type() == IfcUtil::Argument_ENUMERATION) {
        return as<EnumerationReference>().enumeration_value;
    }
    return as<std::string>();
}
IfcWriteArgument::operator IfcUtil::IfcBaseClass*() const { return as<IfcUtil::IfcBaseClass*>(); }
IfcWriteArgument::operator boost::dynamic_bitset<>() const { return as<boost::dynamic_bitset<>>(); }
IfcWriteArgument::operator std::vector<double>() const { return as<std::vector<double>>(); }
IfcWriteArgument::operator std::vector<int>() const { return as<std::vector<int>>(); }
IfcWriteArgument::operator std::vector<std::string>() const { return as<std::vector<std::string>>(); }
IfcWriteArgument::operator std::vector<boost::dynamic_bitset<>>() const { return as<std::vector<boost::dynamic_bitset<>>>(); }
IfcWriteArgument::operator aggregate_of_instance::ptr() const { return as<aggregate_of_instance::ptr>(); }
IfcWriteArgument::operator std::vector<std::vector<int>>() const { return as<std::vector<std::vector<int>>>(); }
IfcWriteArgument::operator std::vector<std::vector<double>>() const { return as<std::vector<std::vector<double>>>(); }
IfcWriteArgument::operator aggregate_of_aggregate_of_instance::ptr() const { return as<aggregate_of_aggregate_of_instance::ptr>(); }
bool IfcWriteArgument::isNull() const { return type() == IfcUtil::Argument_NULL; }
Argument* IfcWriteArgument::operator[](unsigned int /*i*/) const { throw IfcParse::IfcException("Invalid cast"); }
std::string IfcWriteArgument::toString(bool upper) const {
    std::ostringstream str;
    str.imbue(std::locale::classic());
    StringBuilderVisitor visitor(str, upper);
    container_.apply_visitor(visitor);
    return visitor;
}
unsigned int IfcWriteArgument::size() const {
    SizeVisitor visitor;
    const int size = container_.apply_visitor(visitor);
    if (size == -1) {
        throw IfcParse::IfcException("Invalid cast");
    }
    return size;
}

IfcUtil::ArgumentType IfcWriteArgument::type() const {
    return static_cast<IfcUtil::ArgumentType>(container_.which());
}

// Overload to detect null values
void IfcWriteArgument::set(const aggregate_of_instance::ptr& value) {
    if (value) {
        container_ = value;
    } else {
        container_ = boost::blank();
    }
}

// Overload to detect null values
void IfcWriteArgument::set(const aggregate_of_aggregate_of_instance::ptr& value) {
    if (value) {
        container_ = value;
    } else {
        container_ = boost::blank();
    }
}

// Overload to detect null values
void IfcWriteArgument::set(IfcUtil::IfcBaseInterface* const& value) {
    if (value != nullptr) {
        container_ = value->as<IfcUtil::IfcBaseClass>();
    } else {
        container_ = boost::blank();
    }
}

// Overloads to raise exceptions on non-finite values
void IfcWriteArgument::set(const double& v) {
	if (!std::isfinite(v)) {
		throw IfcParse::IfcException("Only finite values are allowed");
	}
	container_ = v;
}

void IfcWriteArgument::set(const std::vector<double>& v) {
	if (std::any_of(v.begin(), v.end(), [](double v) {return !std::isfinite(v); })) {
		throw IfcParse::IfcException("Only finite values are allowed");
	}
    container_ = v;
}

void IfcWriteArgument::set(const std::vector< std::vector<double> >& v) {
	if (std::any_of(v.begin(), v.end(), [](const std::vector<double>& vs) {
		return std::any_of(vs.begin(), vs.end(), [](double v) {return !std::isfinite(v); });
	})) {
		throw IfcParse::IfcException("Only finite values are allowed");
	}
    container_ = v;
}
