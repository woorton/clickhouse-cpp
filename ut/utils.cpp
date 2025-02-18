#include "utils.h"

#include <clickhouse/block.h>
#include <clickhouse/columns/column.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/decimal.h>
#include <clickhouse/columns/enum.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <clickhouse/columns/ip4.h>
#include <clickhouse/columns/ip6.h>

#include <clickhouse/base/socket.h> // for ipv4-ipv6 platform-specific stuff

#include <iomanip>
#include <sstream>

namespace {
using namespace clickhouse;
struct DateTimeValue {
    explicit DateTimeValue(const time_t & v)
        : value(v)
    {}

    template <typename T>
    explicit DateTimeValue(const T & v)
        : value(v)
    {}

    const time_t value;
};

std::ostream& operator<<(std::ostream & ostr, const DateTimeValue & time) {
    const auto t = std::localtime(&time.value);
    char buffer[] = "2015-05-18 07:40:12\0\0";
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", t);

    return ostr << buffer;
}

template <typename ColumnType, typename AsType = typename ColumnType::ValueType>
bool doPrintValue(const ColumnRef & c, const size_t row, std::ostream & ostr) {
    if (const auto & casted_c = c->As<ColumnType>()) {
        ostr << static_cast<AsType>(casted_c->At(row));
        return true;
    }
    return false;
}

template <typename ColumnEnumType>
bool doPrintEnumValue(const ColumnRef & c, const size_t row, std::ostream & ostr) {
    if (const auto & casted_c = c->As<ColumnEnumType>()) {
        // via temporary stream to preserve fill and alignment of the ostr
        std::stringstream sstr;
        sstr << casted_c->NameAt(row) << " (" << static_cast<int64_t>(casted_c->At(row)) << ")";
        ostr << sstr.str();
        return true;
    }
    return false;
}

template <>
bool doPrintValue<ColumnEnum8>(const ColumnRef & c, const size_t row, std::ostream & ostr) {
    return doPrintEnumValue<ColumnEnum8>(c, row, ostr);
}

template <>
bool doPrintValue<ColumnEnum16>(const ColumnRef & c, const size_t row, std::ostream & ostr) {
    return doPrintEnumValue<ColumnEnum16>(c, row, ostr);
}

std::ostream & printColumnValue(const ColumnRef& c, const size_t row, std::ostream & ostr) {

    const auto r = false
        || doPrintValue<ColumnString>(c, row, ostr)
        || doPrintValue<ColumnFixedString>(c, row, ostr)
        || doPrintValue<ColumnUInt8, unsigned int>(c, row, ostr)
        || doPrintValue<ColumnUInt32>(c, row, ostr)
        || doPrintValue<ColumnUInt16>(c, row, ostr)
        || doPrintValue<ColumnUInt64>(c, row, ostr)
        || doPrintValue<ColumnInt8, int>(c, row, ostr)
        || doPrintValue<ColumnInt32>(c, row, ostr)
        || doPrintValue<ColumnInt16>(c, row, ostr)
        || doPrintValue<ColumnInt64>(c, row, ostr)
        || doPrintValue<ColumnFloat32>(c, row, ostr)
        || doPrintValue<ColumnFloat64>(c, row, ostr)
        || doPrintValue<ColumnEnum8>(c, row, ostr)
        || doPrintValue<ColumnEnum16>(c, row, ostr)
        || doPrintValue<ColumnDate, DateTimeValue>(c, row, ostr)
        || doPrintValue<ColumnDateTime, DateTimeValue>(c, row, ostr)
        || doPrintValue<ColumnDateTime64, DateTimeValue>(c, row, ostr)
        || doPrintValue<ColumnDecimal>(c, row, ostr)
        || doPrintValue<ColumnIPv4>(c, row, ostr)
        || doPrintValue<ColumnIPv6>(c, row, ostr);
    if (!r)
        ostr << "Unable to print value of type " << c->GetType().GetName();

    return ostr;
}

struct ColumnValue {
    const ColumnRef& c;
    size_t row;
};

std::ostream & operator<<(std::ostream & ostr, const ColumnValue& v) {
    return printColumnValue(v.c, v.row, ostr);
}

}

std::ostream& operator<<(std::ostream & ostr, const Block & block) {
    if (block.GetRowCount() == 0 || block.GetColumnCount() == 0)
        return ostr;

    for (size_t col = 0; col < block.GetColumnCount(); ++col) {
        const auto & c = block[col];
        ostr << c->GetType().GetName() << " [";

        for (size_t row = 0; row < block.GetRowCount(); ++row) {
            printColumnValue(c, row, ostr);
            if (row != block.GetRowCount() - 1)
                ostr << ", ";
        }
        ostr << "]";

        if (col != block.GetColumnCount() - 1)
            ostr << "\n";
    }

    return ostr;
}

std::ostream& operator<<(std::ostream & ostr, const PrettyPrintBlock & pretty_print_block) {
    // Pretty-print block:
    // - names of each column
    // - types of each column
    // - values of column row-by-row

    const auto & block = pretty_print_block.block;
    if (block.GetRowCount() == 0 || block.GetColumnCount() == 0)
        return ostr;

    std::vector<int> column_width(block.GetColumnCount());
    const auto horizontal_bar = '|';
    const auto cross = '+';
    const auto vertical_bar = '-';

    std::stringstream sstr;
    for (auto i = block.begin(); i != block.end(); ++i) {
        auto width = column_width[i.ColumnIndex()] = std::max(i.Type()->GetName().size(), i.Name().size());
        sstr << cross << std::setw(width + 2) << std::setfill(vertical_bar) << vertical_bar;
    }
    sstr << cross;
    const std::string split_line(sstr.str());

    ostr << split_line << std::endl;
    // column name
    for (auto i = block.begin(); i != block.end(); ++i) {
        auto width = column_width[i.ColumnIndex()];
        ostr << horizontal_bar << ' ' << std::setw(width) << i.Name() << ' ';
    }
    ostr << horizontal_bar << std::endl;;
    ostr << split_line << std::endl;

    // column type
    for (auto i = block.begin(); i != block.end(); ++i) {
        auto width = column_width[i.ColumnIndex()];
        ostr << horizontal_bar << ' ' << std::setw(width) << i.Type()->GetName() << ' ';
    }
    ostr << horizontal_bar << std::endl;;
    ostr << split_line << std::endl;

    // values
    for (size_t row_index = 0; row_index < block.GetRowCount(); ++row_index) {
        for (auto i = block.begin(); i != block.end(); ++i) {
            auto width = column_width[i.ColumnIndex()];
            ostr << horizontal_bar << ' ' << std::setw(width) << ColumnValue{i.Column(), row_index} << ' ';
        }
        ostr << horizontal_bar << std::endl;
    }
    ostr << split_line << std::endl;

    return ostr;
}

std::ostream& operator<<(std::ostream& ostr, const in_addr& addr) {
    char buf[INET_ADDRSTRLEN];
    const char* ip_str = inet_ntop(AF_INET, &addr, buf, sizeof(buf));

    if (!ip_str)
        return ostr << "<!INVALID IPv4 VALUE!>";

    return ostr << ip_str;
}

std::ostream& operator<<(std::ostream& ostr, const in6_addr& addr) {
    char buf[INET6_ADDRSTRLEN];
    const char* ip_str = inet_ntop(AF_INET6, &addr, buf, sizeof(buf));

    if (!ip_str)
        return ostr << "<!INVALID IPv6 VALUE!>";

    return ostr << ip_str;
}
