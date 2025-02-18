#include <clickhouse/columns/array.h>
#include <clickhouse/columns/tuple.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/enum.h>
#include <clickhouse/columns/factory.h>
#include <clickhouse/columns/lowcardinality.h>
#include <clickhouse/columns/nullable.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <clickhouse/columns/uuid.h>
#include <clickhouse/columns/ip4.h>
#include <clickhouse/columns/ip6.h>
#include <clickhouse/base/input.h>
#include <clickhouse/base/output.h>
#include <clickhouse/base/socket.h> // for ipv4-ipv6 platform-specific stuff

#include <gtest/gtest.h>
#include "utils.h"

#include <string_view>
#include <sstream>


// only compare PODs of equal size this way
template <typename L, typename R, typename
        = std::enable_if_t<sizeof(L) == sizeof(R) && std::conjunction_v<std::is_pod<L>, std::is_pod<R>>>>
bool operator==(const L & left, const R& right) {
    return memcmp(&left, &right, sizeof(left)) == 0;
}

bool operator==(const in6_addr & left, const std::string_view & right) {
    return right.size() == sizeof(left) && memcmp(&left, right.data(), sizeof(left)) == 0;
}

bool operator==(const std::string_view & left, const in6_addr & right) {
    return left.size() == sizeof(right) && memcmp(left.data(), &right, sizeof(right)) == 0;
}

namespace {

using namespace clickhouse;
using namespace std::literals::string_view_literals;

in_addr MakeIPv4(uint32_t ip) {
    static_assert(sizeof(in_addr) == sizeof(ip));
    in_addr result;
    memcpy(&result, &ip, sizeof(ip));

    return result;
}

in6_addr MakeIPv6(uint8_t v0,  uint8_t v1,  uint8_t v2,  uint8_t v3,
                   uint8_t v4,  uint8_t v5,  uint8_t v6,  uint8_t v7,
                   uint8_t v8,  uint8_t v9,  uint8_t v10, uint8_t v11,
                   uint8_t v12, uint8_t v13, uint8_t v14, uint8_t v15) {
    return in6_addr{{{v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15}}};
}

in6_addr MakeIPv6(uint8_t v10, uint8_t v11, uint8_t v12, uint8_t v13, uint8_t v14, uint8_t v15) {
    return in6_addr{{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, v10, v11, v12, v13, v14, v15}}};
}

static std::vector<uint32_t> MakeNumbers() {
    return std::vector<uint32_t>
        {1, 2, 3, 7, 11, 13, 17, 19, 23, 29, 31};
}

static std::vector<uint8_t> MakeBools() {
    return std::vector<uint8_t>
        {1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0};
}

static std::vector<std::string> MakeFixedStrings() {
    return std::vector<std::string>
        {"aaa", "bbb", "ccc", "ddd"};
}

static std::vector<std::string> MakeStrings() {
    return std::vector<std::string>
        {"a", "ab", "abc", "abcd"};
}

static std::vector<uint64_t> MakeUUIDs() {
    return std::vector<uint64_t>
        {0xbb6a8c699ab2414cllu, 0x86697b7fd27f0825llu,
         0x84b9f24bc26b49c6llu, 0xa03b4ab723341951llu,
         0x3507213c178649f9llu, 0x9faf035d662f60aellu};
}

static const auto LOWCARDINALITY_STRING_FOOBAR_10_ITEMS_BINARY =
        "\x01\x00\x00\x00\x00\x00\x00\x00\x00\x06\x00\x00\x00\x00\x00\x00"
        "\x09\x00\x00\x00\x00\x00\x00\x00\x00\x06\x46\x6f\x6f\x42\x61\x72"
        "\x01\x31\x01\x32\x03\x46\x6f\x6f\x01\x34\x03\x42\x61\x72\x01\x37"
        "\x01\x38\x0a\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06"
        "\x04\x07\x08\x04"sv;

template <typename Generator>
auto GenerateVector(size_t items, Generator && gen) {
    std::vector<my_result_of_t<Generator, size_t>> result;
    result.reserve(items);
    for (size_t i = 0; i < items; ++i) {
        result.push_back(std::move(gen(i)));
    }

    return result;
}

std::string FooBarSeq(size_t i) {
    std::string result;
    if (i % 3 == 0)
        result += "Foo";
    if (i % 5 == 0)
        result += "Bar";
    if (result.empty())
        result = std::to_string(i);

    return result;
}

template <typename T, typename U = T>
auto SameValueSeq(const U & value) {
    return [&value](size_t) -> T {
        return value;
    };
}

template <typename ResultType, typename Generator1, typename Generator2>
auto AlternateGenerators(Generator1 && gen1, Generator2 && gen2) {
    return [&gen1, &gen2](size_t i) -> ResultType {
        if (i % 2 == 0)
            return gen1(i/2);
        else
            return gen2(i/2);
    };
}

template <typename T>
std::vector<T> ConcatSequences(std::vector<T> && vec1, std::vector<T> && vec2) {
    std::vector<T> result(vec1);

    result.reserve(vec1.size() + vec2.size());
    result.insert(result.end(), vec2.begin(), vec2.end());

    return result;
}

static std::vector<Int64> MakeDateTime64s() {
    static const auto seconds_multiplier = 1'000'000;
    static const auto year = 86400ull * 365 * seconds_multiplier; // ~approx, but this doesn't matter here.

    // Approximatelly +/- 200 years around epoch (and value of epoch itself)
    // with non zero seconds and sub-seconds.
    // Please note there are values outside of DateTime (32-bit) range that might
    // not have correct string representation in CH yet,
    // but still are supported as Int64 values.
    return GenerateVector(200,
        [] (size_t i )-> Int64 {
            return (i - 100) * year * 2 + (i * 10) * seconds_multiplier + i;
        });
}

}

// TODO: add tests for ColumnDecimal.

TEST(ColumnsCase, NumericInit) {
    auto col = std::make_shared<ColumnUInt32>(MakeNumbers());

    ASSERT_EQ(col->Size(), 11u);
    ASSERT_EQ(col->At(3),   7u);
    ASSERT_EQ(col->At(10), 31u);

    auto sun = std::make_shared<ColumnUInt32>(MakeNumbers());
}

TEST(ColumnsCase, NumericSlice) {
    auto col = std::make_shared<ColumnUInt32>(MakeNumbers());
    auto sub = col->Slice(3, 3)->As<ColumnUInt32>();

    ASSERT_EQ(sub->Size(), 3u);
    ASSERT_EQ(sub->At(0),  7u);
    ASSERT_EQ(sub->At(2), 13u);
}


TEST(ColumnsCase, FixedStringInit) {
    const auto column_data = MakeFixedStrings();
    auto col = std::make_shared<ColumnFixedString>(3, column_data);

    ASSERT_EQ(col->Size(), column_data.size());

    size_t i = 0;
    for (const auto& s : column_data) {
        EXPECT_EQ(s, col->At(i));
        ++i;
    }
}

TEST(ColumnsCase, FixedString_Append_SmallStrings) {
    // Ensure that strings smaller than FixedString's size
    // are padded with zeroes on insertion.

    const size_t string_size = 7;
    const auto column_data = MakeFixedStrings();

    auto col = std::make_shared<ColumnFixedString>(string_size);
    size_t i = 0;
    for (const auto& s : column_data) {
        col->Append(s);

        EXPECT_EQ(string_size, col->At(i).size());

        std::string expected = column_data[i];
        expected.resize(string_size, char(0));
        EXPECT_EQ(expected, col->At(i));

        ++i;
    }

    ASSERT_EQ(col->Size(), i);
}

TEST(ColumnsCase, FixedString_Append_LargeString) {
    // Ensure that inserting strings larger than FixedString size thorws exception.

    const auto col = std::make_shared<ColumnFixedString>(1);
    EXPECT_ANY_THROW(col->Append("2c"));
    EXPECT_ANY_THROW(col->Append("this is a long string"));
}

TEST(ColumnsCase, StringInit) {
    auto col = std::make_shared<ColumnString>(MakeStrings());

    ASSERT_EQ(col->Size(), 4u);
    ASSERT_EQ(col->At(1), "ab");
    ASSERT_EQ(col->At(3), "abcd");
}


TEST(ColumnsCase, ArrayAppend) {
    auto arr1 = std::make_shared<ColumnArray>(std::make_shared<ColumnUInt64>());
    auto arr2 = std::make_shared<ColumnArray>(std::make_shared<ColumnUInt64>());

    auto id = std::make_shared<ColumnUInt64>();
    id->Append(1);
    arr1->AppendAsColumn(id);

    id->Append(3);
    arr2->AppendAsColumn(id);

    arr1->Append(arr2);

    auto col = arr1->GetAsColumn(1);

    ASSERT_EQ(arr1->Size(), 2u);
    ASSERT_EQ(col->As<ColumnUInt64>()->At(0), 1u);
    ASSERT_EQ(col->As<ColumnUInt64>()->At(1), 3u);
}

TEST(ColumnsCase, TupleAppend){
    auto tuple1 = std::make_shared<ColumnTuple>(std::vector<ColumnRef>({
                                std::make_shared<ColumnUInt64>(),
                                std::make_shared<ColumnString>()
                            }));
    auto tuple2 = std::make_shared<ColumnTuple>(std::vector<ColumnRef>({
                                std::make_shared<ColumnUInt64>(),
                                std::make_shared<ColumnString>()
                            }));
    (*tuple1)[0]->As<ColumnUInt64>()->Append(2u);
    (*tuple1)[1]->As<ColumnString>()->Append("2");
    tuple2->Append(tuple1);

    ASSERT_EQ((*tuple2)[0]->As<ColumnUInt64>()->At(0), 2u);
    ASSERT_EQ((*tuple2)[1]->As<ColumnString>()->At(0), "2");
}

TEST(ColumnsCase, TupleSlice){
    auto tuple1 = std::make_shared<ColumnTuple>(std::vector<ColumnRef>({
                                std::make_shared<ColumnUInt64>(),
                                std::make_shared<ColumnString>()
                            }));

    (*tuple1)[0]->As<ColumnUInt64>()->Append(2u);
    (*tuple1)[1]->As<ColumnString>()->Append("2");
    (*tuple1)[0]->As<ColumnUInt64>()->Append(3u);
    (*tuple1)[1]->As<ColumnString>()->Append("3");
    auto tuple2 = tuple1->Slice(1,1)->As<ColumnTuple>();

    ASSERT_EQ((*tuple2)[0]->As<ColumnUInt64>()->At(0), 3u);
    ASSERT_EQ((*tuple2)[1]->As<ColumnString>()->At(0), "3");
}


TEST(ColumnsCase, DateAppend) {
    auto col1 = std::make_shared<ColumnDate>();
    auto col2 = std::make_shared<ColumnDate>();
    auto now  = std::time(nullptr);

    col1->Append(now);
    col2->Append(col1);

    ASSERT_EQ(col2->Size(), 1u);
    ASSERT_EQ(col2->At(0), (now / 86400) * 86400);
}

TEST(ColumnsCase, DateTime64_0) {
    auto column = std::make_shared<ColumnDateTime64>(0ul);

    ASSERT_EQ(Type::DateTime64, column->Type()->GetCode());
    ASSERT_EQ("DateTime64(0)", column->Type()->GetName());
    ASSERT_EQ(0u, column->GetPrecision());
    ASSERT_EQ(0u, column->Size());
}

TEST(ColumnsCase, DateTime64_6) {
    auto column = std::make_shared<ColumnDateTime64>(6ul);

    ASSERT_EQ(Type::DateTime64, column->Type()->GetCode());
    ASSERT_EQ("DateTime64(6)", column->Type()->GetName());
    ASSERT_EQ(6u, column->GetPrecision());
    ASSERT_EQ(0u, column->Size());
}

TEST(ColumnsCase, DateTime64_Append_At) {
    auto column = std::make_shared<ColumnDateTime64>(6ul);

    const auto data = MakeDateTime64s();
    for (const auto & v : data) {
        column->Append(v);
    }

    ASSERT_EQ(data.size(), column->Size());
    for (size_t i = 0; i < data.size(); ++i) {
        ASSERT_EQ(data[i], column->At(i));
    }
}

TEST(ColumnsCase, DateTime64_Clear) {
    auto column = std::make_shared<ColumnDateTime64>(6ul);

    // Clearing empty column doesn't crash and produces expected result
    ASSERT_NO_THROW(column->Clear());
    ASSERT_EQ(0u, column->Size());

    const auto data = MakeDateTime64s();
    for (const auto & v : data) {
        column->Append(v);
    }

    ASSERT_NO_THROW(column->Clear());
    ASSERT_EQ(0u, column->Size());
}

TEST(ColumnsCase, DateTime64_Swap) {
    auto column = std::make_shared<ColumnDateTime64>(6ul);

    const auto data = MakeDateTime64s();
    for (const auto & v : data) {
        column->Append(v);
    }

    auto column2 = std::make_shared<ColumnDateTime64>(6ul);
    const auto single_dt64_value = 1'234'567'890'123'456'789ll;
    column2->Append(single_dt64_value);
    column->Swap(*column2);

    // Validate that all items were transferred to column2.
    ASSERT_EQ(1u, column->Size());
    EXPECT_EQ(single_dt64_value, column->At(0));

    ASSERT_EQ(data.size(), column2->Size());
    for (size_t i = 0; i < data.size(); ++i) {
        ASSERT_EQ(data[i], column2->At(i));
    }
}

TEST(ColumnsCase, DateTime64_Slice) {
    auto column = std::make_shared<ColumnDateTime64>(6ul);

    {
        // Empty slice on empty column
        auto slice = column->Slice(0, 0)->As<ColumnDateTime64>();
        ASSERT_EQ(0u, slice->Size());
        ASSERT_EQ(column->GetPrecision(), slice->GetPrecision());
    }

    const auto data = MakeDateTime64s();
    const size_t size = data.size();
    ASSERT_GT(size, 4u); // so the partial slice below has half of the elements of the column

    for (const auto & v : data) {
        column->Append(v);
    }

    {
        // Empty slice on non-empty column
        auto slice = column->Slice(0, 0)->As<ColumnDateTime64>();
        ASSERT_EQ(0u, slice->Size());
        ASSERT_EQ(column->GetPrecision(), slice->GetPrecision());
    }

    {
        // Full-slice on non-empty column
        auto slice = column->Slice(0, size)->As<ColumnDateTime64>();
        ASSERT_EQ(column->Size(), slice->Size());
        ASSERT_EQ(column->GetPrecision(), slice->GetPrecision());

        for (size_t i = 0; i < data.size(); ++i) {
            ASSERT_EQ(data[i], slice->At(i));
        }
    }

    {
        const size_t offset = size / 4;
        const size_t count = size / 2;
        // Partial slice on non-empty column
        auto slice = column->Slice(offset, count)->As<ColumnDateTime64>();

        ASSERT_EQ(count, slice->Size());
        ASSERT_EQ(column->GetPrecision(), slice->GetPrecision());

        for (size_t i = offset; i < offset + count; ++i) {
            ASSERT_EQ(data[i], slice->At(i - offset));
        }
    }
}

TEST(ColumnsCase, DateTime64_Slice_OUTOFBAND) {
    // Slice() shouldn't throw exceptions on invalid parameters, just clamp values to the nearest bounds.

    auto column = std::make_shared<ColumnDateTime64>(6ul);

    // Non-Empty slice on empty column
    EXPECT_EQ(0u, column->Slice(0, 10)->Size());

    const auto data = MakeDateTime64s();
    for (const auto & v : data) {
        column->Append(v);
    }

    EXPECT_EQ(column->Slice(0, data.size() + 1)->Size(), data.size());
    EXPECT_EQ(column->Slice(data.size() + 1, 1)->Size(), 0u);
    EXPECT_EQ(column->Slice(data.size() / 2, data.size() / 2 + 2)->Size(), data.size() - data.size() / 2);
}

TEST(ColumnsCase, DateTime64_Swap_EXCEPTION) {
    auto column1 = std::make_shared<ColumnDateTime64>(6ul);
    auto column2 = std::make_shared<ColumnDateTime64>(0ul);

    EXPECT_ANY_THROW(column1->Swap(*column2));
}

TEST(ColumnsCase, Date2038) {
    auto col1 = std::make_shared<ColumnDate>();
    const std::time_t largeDate(25882ull * 86400ull);
    col1->Append(largeDate);

    ASSERT_EQ(col1->Size(), 1u);
    ASSERT_EQ(largeDate, col1->At(0));
}

TEST(ColumnsCase, DateTime) {
    ASSERT_NE(nullptr, CreateColumnByType("DateTime"));
    ASSERT_NE(nullptr, CreateColumnByType("DateTime('Europe/Moscow')"));

    ASSERT_EQ(CreateColumnByType("DateTime('UTC')")->As<ColumnDateTime>()->Timezone(), "UTC");
    ASSERT_EQ(CreateColumnByType("DateTime64(3, 'UTC')")->As<ColumnDateTime64>()->Timezone(), "UTC");
}

TEST(ColumnsCase, EnumTest) {
    std::vector<Type::EnumItem> enum_items = {{"Hi", 1}, {"Hello", 2}};

    auto col = std::make_shared<ColumnEnum8>(Type::CreateEnum8(enum_items));
    ASSERT_TRUE(col->Type()->IsEqual(Type::CreateEnum8(enum_items)));

    col->Append(1);
    ASSERT_EQ(col->Size(), 1u);
    ASSERT_EQ(col->At(0), 1);
    ASSERT_EQ(col->NameAt(0), "Hi");

    col->Append("Hello");
    ASSERT_EQ(col->Size(), 2u);
    ASSERT_EQ(col->At(1), 2);
    ASSERT_EQ(col->NameAt(1), "Hello");

    auto col16 = std::make_shared<ColumnEnum16>(Type::CreateEnum16(enum_items));
    ASSERT_TRUE(col16->Type()->IsEqual(Type::CreateEnum16(enum_items)));

    ASSERT_TRUE(CreateColumnByType("Enum8('Hi' = 1, 'Hello' = 2)")->Type()->IsEqual(Type::CreateEnum8(enum_items)));
}

TEST(ColumnsCase, NullableSlice) {
    auto data = std::make_shared<ColumnUInt32>(MakeNumbers());
    auto nulls = std::make_shared<ColumnUInt8>(MakeBools());
    auto col = std::make_shared<ColumnNullable>(data, nulls);
    auto sub = col->Slice(3, 4)->As<ColumnNullable>();
    auto subData = sub->Nested()->As<ColumnUInt32>();

    ASSERT_EQ(sub->Size(), 4u);
    ASSERT_FALSE(sub->IsNull(0));
    ASSERT_EQ(subData->At(0),  7u);
    ASSERT_TRUE(sub->IsNull(1));
    ASSERT_FALSE(sub->IsNull(3));
    ASSERT_EQ(subData->At(3), 17u);
}

TEST(ColumnsCase, UUIDInit) {
    auto col = std::make_shared<ColumnUUID>(std::make_shared<ColumnUInt64>(MakeUUIDs()));

    ASSERT_EQ(col->Size(), 3u);
    ASSERT_EQ(col->At(0), UInt128(0xbb6a8c699ab2414cllu, 0x86697b7fd27f0825llu));
    ASSERT_EQ(col->At(2), UInt128(0x3507213c178649f9llu, 0x9faf035d662f60aellu));
}

TEST(ColumnsCase, UUIDSlice) {
    auto col = std::make_shared<ColumnUUID>(std::make_shared<ColumnUInt64>(MakeUUIDs()));
    auto sub = col->Slice(1, 2)->As<ColumnUUID>();

    ASSERT_EQ(sub->Size(), 2u);
    ASSERT_EQ(sub->At(0), UInt128(0x84b9f24bc26b49c6llu, 0xa03b4ab723341951llu));
    ASSERT_EQ(sub->At(1), UInt128(0x3507213c178649f9llu, 0x9faf035d662f60aellu));
}

TEST(ColumnsCase, Int128) {
    auto col = std::make_shared<ColumnInt128>(std::vector<Int128>{
            absl::MakeInt128(0xffffffffffffffffll, 0xffffffffffffffffll), // -1
            absl::MakeInt128(0, 0xffffffffffffffffll),  // 2^64
            absl::MakeInt128(0xffffffffffffffffll, 0),
            absl::MakeInt128(0x8000000000000000ll, 0),
            Int128(0)
    });

    EXPECT_EQ(-1, col->At(0));

    EXPECT_EQ(absl::MakeInt128(0, 0xffffffffffffffffll), col->At(1));
    EXPECT_EQ(0ll,                   absl::Int128High64(col->At(1)));
    EXPECT_EQ(0xffffffffffffffffull, absl::Int128Low64(col->At(1)));

    EXPECT_EQ(absl::MakeInt128(0xffffffffffffffffll, 0), col->At(2));
    EXPECT_EQ(static_cast<int64_t>(0xffffffffffffffffll),  absl::Int128High64(col->At(2)));
    EXPECT_EQ(0ull,                  absl::Int128Low64(col->At(2)));

    EXPECT_EQ(0, col->At(4));
}

TEST(ColumnsCase, ColumnIPv4)
{
    // TODO: split into proper method-level unit-tests
    auto col = ColumnIPv4();

    col.Append("255.255.255.255");
    col.Append("127.0.0.1");
    col.Append(3585395774);
    col.Append(0);
    const in_addr ip = MakeIPv4(0x12345678);
    col.Append(ip);

    ASSERT_EQ(5u, col.Size());
    EXPECT_EQ(MakeIPv4(0xffffffff), col.At(0));
    EXPECT_EQ(MakeIPv4(0x0100007f), col.At(1));
    EXPECT_EQ(MakeIPv4(3585395774), col.At(2));
    EXPECT_EQ(MakeIPv4(0),          col.At(3));
    EXPECT_EQ(ip,                  col.At(4));

    EXPECT_EQ("255.255.255.255", col.AsString(0));
    EXPECT_EQ("127.0.0.1",       col.AsString(1));
    EXPECT_EQ("62.204.180.213",  col.AsString(2));
    EXPECT_EQ("0.0.0.0",         col.AsString(3));
    EXPECT_EQ("120.86.52.18",    col.AsString(4));

    col.Clear();
    EXPECT_EQ(0u, col.Size());
}

TEST(ColumnsCase, ColumnIPv4_construct_from_data)
{
    const auto vals = {
        MakeIPv4(0x12345678),
        MakeIPv4(0x0),
        MakeIPv4(0x0100007f)
    };

    {
        // Column is usable after being initialized with empty data column
        auto col = ColumnIPv4(std::make_shared<ColumnUInt32>());
        EXPECT_EQ(0u, col.Size());

        // Make sure that `Append` and `At`/`[]` work properly
        size_t i = 0;
        for (const auto & v : vals) {
            col.Append(v);
            EXPECT_EQ(v, col[col.Size() - 1]) << "At pos " << i;
            EXPECT_EQ(v, col.At(col.Size() - 1)) << "At pos " << i;
            ++i;
        }

        EXPECT_EQ(vals.size(), col.Size());
    }

    {
        // Column reports values from data column exactly, and also can be modified afterwards.
        const auto values = std::vector<uint32_t>{std::numeric_limits<uint32_t>::min(), 123, 456, 789101112, std::numeric_limits<uint32_t>::max()};
        auto col = ColumnIPv4(std::make_shared<ColumnUInt32>(values));

        EXPECT_EQ(values.size(), col.Size());
        for (size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(ntohl(values[i]), col[i]) << " At pos: " << i;
        }

        // Make sure that `Append` and `At`/`[]` work properly
        size_t i = 0;
        for (const auto & v : vals) {
            col.Append(v);
            EXPECT_EQ(v, col[col.Size() - 1]) << "At pos " << i;
            EXPECT_EQ(v, col.At(col.Size() - 1)) << "At pos " << i;
            ++i;
        }

        EXPECT_EQ(values.size() + vals.size(), col.Size());
    }

    EXPECT_ANY_THROW(ColumnIPv4(nullptr));
    EXPECT_ANY_THROW(ColumnIPv4(ColumnRef(std::make_shared<ColumnInt8>())));
    EXPECT_ANY_THROW(ColumnIPv4(ColumnRef(std::make_shared<ColumnInt32>())));

    EXPECT_ANY_THROW(ColumnIPv4(ColumnRef(std::make_shared<ColumnUInt8>())));

    EXPECT_ANY_THROW(ColumnIPv4(ColumnRef(std::make_shared<ColumnInt128>())));
    EXPECT_ANY_THROW(ColumnIPv4(ColumnRef(std::make_shared<ColumnString>())));
}

TEST(ColumnsCase, ColumnIPv6)
{
    // TODO: split into proper method-level unit-tests
    auto col = ColumnIPv6();
    col.Append("0:0:0:0:0:0:0:1");
    col.Append("::");
    col.Append("::FFFF:204.152.189.116");

    const auto ipv6 = MakeIPv6(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    col.Append(ipv6);

    ASSERT_EQ(4u, col.Size());
    EXPECT_EQ(MakeIPv6(0, 0, 0, 0, 0, 1),               col.At(0));
    EXPECT_EQ(MakeIPv6(0, 0, 0, 0, 0, 0),               col.At(1));
    EXPECT_EQ(MakeIPv6(0xff, 0xff, 204, 152, 189, 116), col.At(2));

    EXPECT_EQ(ipv6, col.At(3));

    EXPECT_EQ("::1",                    col.AsString(0));
    EXPECT_EQ("::",                     col.AsString(1));
    EXPECT_EQ("::ffff:204.152.189.116", col.AsString(2));
    EXPECT_EQ("1:203:405:607:809:a0b:c0d:e0f", col.AsString(3));

    col.Clear();
    EXPECT_EQ(0u, col.Size());
}

TEST(ColumnsCase, ColumnIPv6_construct_from_data)
{
    const auto vals = {
        MakeIPv6(0xff, 0xff, 204, 152, 189, 116),
        MakeIPv6(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    };

    {
        // Column is usable after being initialized with empty data column
        auto col = ColumnIPv6(std::make_shared<ColumnFixedString>(16));
        EXPECT_EQ(0u, col.Size());

        // Make sure that `Append` and `At`/`[]` work properly
        size_t i = 0;
        for (const auto & v : vals) {
            col.Append(v);
            EXPECT_EQ(v, col[col.Size() - 1]) << "At pos " << i;
            EXPECT_EQ(v, col.At(col.Size() - 1)) << "At pos " << i;
            ++i;
        }

        EXPECT_EQ(vals.size(), col.Size());
    }

    {
        // Column reports values from data column exactly, and also can be modified afterwards.
        using namespace std::literals;
        const auto values = std::vector<std::string_view>{
                "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"sv,
                "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"sv,
                "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF"sv};
        auto col = ColumnIPv6(std::make_shared<ColumnFixedString>(16, values));

        EXPECT_EQ(values.size(), col.Size());
        for (size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i], col[i]) << " At pos: " << i;
        }

        // Make sure that `Append` and `At`/`[]` work properly
        size_t i = 0;
        for (const auto & v : vals) {
            col.Append(v);
            EXPECT_EQ(v, col[col.Size() - 1]) << "At pos " << i;
            EXPECT_EQ(v, col.At(col.Size() - 1)) << "At pos " << i;
            ++i;
        }

        EXPECT_EQ(values.size() + vals.size(), col.Size());
    }

    // Make sure that column can't be constructed with wrong data columns (wrong size/wrong type or null)
    EXPECT_ANY_THROW(ColumnIPv4(nullptr));
    EXPECT_ANY_THROW(ColumnIPv6(ColumnRef(std::make_shared<ColumnFixedString>(15))));
    EXPECT_ANY_THROW(ColumnIPv6(ColumnRef(std::make_shared<ColumnFixedString>(17))));

    EXPECT_ANY_THROW(ColumnIPv6(ColumnRef(std::make_shared<ColumnInt8>())));
    EXPECT_ANY_THROW(ColumnIPv6(ColumnRef(std::make_shared<ColumnInt32>())));

    EXPECT_ANY_THROW(ColumnIPv6(ColumnRef(std::make_shared<ColumnUInt8>())));

    EXPECT_ANY_THROW(ColumnIPv6(ColumnRef(std::make_shared<ColumnInt128>())));
    EXPECT_ANY_THROW(ColumnIPv6(ColumnRef(std::make_shared<ColumnString>())));
}

TEST(ColumnsCase, ColumnDecimal128_from_string) {
    auto col = std::make_shared<ColumnDecimal>(38, 0);

    const auto values = {
        Int128(0),
        Int128(-1),
        Int128(1),
        std::numeric_limits<Int128>::min() + 1,
        std::numeric_limits<Int128>::max(),
    };

    for (size_t i = 0; i < values.size(); ++i) {
        const auto value = values.begin()[i];
        SCOPED_TRACE(::testing::Message() << "# index: " << i << " Int128 value: " << value);

        {
            std::stringstream sstr;
            sstr << value;
            const auto string_value = sstr.str();

            EXPECT_NO_THROW(col->Append(string_value));
        }

        ASSERT_EQ(i + 1, col->Size());
        EXPECT_EQ(value, col->At(i));
    }
}

TEST(ColumnsCase, ColumnDecimal128_from_string_overflow) {
    auto col = std::make_shared<ColumnDecimal>(38, 0);

    // 2^128 overflows
    EXPECT_ANY_THROW(col->Append("340282366920938463463374607431768211456"));
    // special case for number bigger than 2^128, ending in zeroes.
    EXPECT_ANY_THROW(col->Append("400000000000000000000000000000000000000"));

#ifndef ABSL_HAVE_INTRINSIC_INT128
    // unfortunatelly std::numeric_limits<Int128>::min() overflows when there is no __int128 intrinsic type.
    EXPECT_ANY_THROW(col->Append("-170141183460469231731687303715884105728"));
#endif
}

TEST(ColumnsCase, ColumnLowCardinalityString_Append_and_Read) {
    const size_t items_count = 11;
    ColumnLowCardinalityT<ColumnString> col;
    for (const auto & item : GenerateVector(items_count, &FooBarSeq)) {
        col.Append(item);
    }

    ASSERT_EQ(col.Size(), items_count);
    ASSERT_EQ(col.GetDictionarySize(), 8u + 1); // 8 unique items from sequence + 1 null-item

    for (size_t i = 0; i < items_count; ++i) {
        ASSERT_EQ(col.At(i), FooBarSeq(i)) << " at pos: " << i;
        ASSERT_EQ(col[i], FooBarSeq(i)) << " at pos: " << i;
    }
}

TEST(ColumnsCase, ColumnLowCardinalityString_Clear_and_Append) {
    const size_t items_count = 11;
    ColumnLowCardinalityT<ColumnString> col;
    for (const auto & item : GenerateVector(items_count, &FooBarSeq))
    {
        col.Append(item);
    }

    col.Clear();
    ASSERT_EQ(col.Size(), 0u);
    ASSERT_EQ(col.GetDictionarySize(), 1u); // null-item

    for (const auto & item : GenerateVector(items_count, &FooBarSeq))
    {
        col.Append(item);
    }

    ASSERT_EQ(col.Size(), items_count);
    ASSERT_EQ(col.GetDictionarySize(), 8u + 1); // 8 unique items from sequence + 1 null-item
}

TEST(ColumnsCase, ColumnLowCardinalityString_Load) {
    const size_t items_count = 10;
    ColumnLowCardinalityT<ColumnString> col;

    const auto & data = LOWCARDINALITY_STRING_FOOBAR_10_ITEMS_BINARY;
    ArrayInput buffer(data.data(), data.size());

    ASSERT_TRUE(col.Load(&buffer, items_count));

    for (size_t i = 0; i < items_count; ++i) {
        EXPECT_EQ(col.At(i), FooBarSeq(i)) << " at pos: " << i;
    }
}

// This is temporary diabled since we are not 100% compatitable with ClickHouse
// on how we serailize LC columns, but we check interoperability in other tests (see client_ut.cpp)
TEST(ColumnsCase, DISABLED_ColumnLowCardinalityString_Save) {
    const size_t items_count = 10;
    ColumnLowCardinalityT<ColumnString> col;
    for (const auto & item : GenerateVector(items_count, &FooBarSeq)) {
        col.Append(item);
    }

    ArrayOutput output(0, 0);

    const size_t expected_output_size = LOWCARDINALITY_STRING_FOOBAR_10_ITEMS_BINARY.size();
    // Enough space to account for possible overflow from both right and left sides.
    std::string buffer(expected_output_size * 10, '\0');// = {'\0'};
    const char margin_content[sizeof(buffer)] = {'\0'};

    const size_t left_margin_size = 10;
    const size_t right_margin_size = sizeof(buffer) - left_margin_size - expected_output_size;

    // Since overflow from left side is less likely to happen, leave only tiny margin there.
    auto write_pos = buffer.data() + left_margin_size;
    const auto left_margin = buffer.data();
    const auto right_margin = write_pos + expected_output_size;

    output.Reset(write_pos, expected_output_size);

    EXPECT_NO_THROW(col.Save(&output));

    // Left margin should be blank
    EXPECT_EQ(std::string_view(margin_content, left_margin_size), std::string_view(left_margin, left_margin_size));
    // Right margin should be blank too
    EXPECT_EQ(std::string_view(margin_content, right_margin_size), std::string_view(right_margin, right_margin_size));

    // TODO: right now LC columns do not write indexes in the most compact way possible, so binary representation is a bit different
    // (there might be other inconsistances too)
    EXPECT_EQ(LOWCARDINALITY_STRING_FOOBAR_10_ITEMS_BINARY, std::string_view(write_pos, expected_output_size));
}

TEST(ColumnsCase, ColumnLowCardinalityString_SaveAndLoad) {
    // Verify that we can load binary representation back
    ColumnLowCardinalityT<ColumnString> col;

    const auto items = GenerateVector(10, &FooBarSeq);
    for (const auto & item : items) {
        col.Append(item);
    }

    char buffer[256] = {'\0'}; // about 3 times more space than needed for this set of values.
    {
        ArrayOutput output(buffer, sizeof(buffer));
        EXPECT_NO_THROW(col.Save(&output));
    }

    col.Clear();

    {
        // Load the data back
        ArrayInput input(buffer, sizeof(buffer));
        EXPECT_TRUE(col.Load(&input, items.size()));
    }

    for (size_t i = 0; i < items.size(); ++i) {
        EXPECT_EQ(col.At(i), items[i]) << " at pos: " << i;
    }
}

TEST(ColumnsCase, ColumnLowCardinalityString_WithEmptyString_1) {
    // Verify that when empty string is added to a LC column it can be retrieved back as empty string.
    ColumnLowCardinalityT<ColumnString> col;
    const auto values = GenerateVector(10, AlternateGenerators<std::string>(SameValueSeq<std::string>(""), FooBarSeq));
    for (const auto & item : values) {
        col.Append(item);
    }

    for (size_t i = 0; i < values.size(); ++i) {
        EXPECT_EQ(values[i], col.At(i)) << " at pos: " << i;
    }
}

TEST(ColumnsCase, ColumnLowCardinalityString_WithEmptyString_2) {
    // Verify that when empty string is added to a LC column it can be retrieved back as empty string.
    // (Ver2): Make sure that outcome doesn't depend if empty values are on odd positions
    ColumnLowCardinalityT<ColumnString> col;
    const auto values = GenerateVector(10, AlternateGenerators<std::string>(FooBarSeq, SameValueSeq<std::string>("")));
    for (const auto & item : values) {
        col.Append(item);
    }

    for (size_t i = 0; i < values.size(); ++i) {
        EXPECT_EQ(values[i], col.At(i)) << " at pos: " << i;
    }
}

TEST(ColumnsCase, ColumnLowCardinalityString_WithEmptyString_3) {
    // When we have many leading empty strings and some non-empty values.
    ColumnLowCardinalityT<ColumnString> col;
    const auto values = ConcatSequences(GenerateVector(100, SameValueSeq<std::string>("")), GenerateVector(5, FooBarSeq));
    for (const auto & item : values) {
        col.Append(item);
    }

    for (size_t i = 0; i < values.size(); ++i) {
        EXPECT_EQ(values[i], col.At(i)) << " at pos: " << i;
    }
}

TEST(ColumnsCase, CreateSimpleAggregateFunction) {
    auto col = CreateColumnByType("SimpleAggregateFunction(funt, Int32)");

    ASSERT_EQ("Int32", col->Type()->GetName());
    ASSERT_EQ(Type::Int32, col->Type()->GetCode());
    ASSERT_NE(nullptr, col->As<ColumnInt32>());
}


TEST(ColumnsCase, UnmatchedBrackets) {
    // When type string has unmatched brackets, CreateColumnByType must return nullptr.
    ASSERT_EQ(nullptr, CreateColumnByType("FixedString(10"));
    ASSERT_EQ(nullptr, CreateColumnByType("Nullable(FixedString(10000"));
    ASSERT_EQ(nullptr, CreateColumnByType("Nullable(FixedString(10000)"));
    ASSERT_EQ(nullptr, CreateColumnByType("LowCardinality(Nullable(FixedString(10000"));
    ASSERT_EQ(nullptr, CreateColumnByType("LowCardinality(Nullable(FixedString(10000)"));
    ASSERT_EQ(nullptr, CreateColumnByType("LowCardinality(Nullable(FixedString(10000))"));
    ASSERT_EQ(nullptr, CreateColumnByType("Array(LowCardinality(Nullable(FixedString(10000"));
    ASSERT_EQ(nullptr, CreateColumnByType("Array(LowCardinality(Nullable(FixedString(10000)"));
    ASSERT_EQ(nullptr, CreateColumnByType("Array(LowCardinality(Nullable(FixedString(10000))"));
    ASSERT_EQ(nullptr, CreateColumnByType("Array(LowCardinality(Nullable(FixedString(10000)))"));
}

TEST(ColumnsCase, LowCardinalityAsWrappedColumn) {
    CreateColumnByTypeSettings create_column_settings;
    create_column_settings.low_cardinality_as_wrapped_column = true;

    ASSERT_EQ(Type::String, CreateColumnByType("LowCardinality(String)", create_column_settings)->GetType().GetCode());
    ASSERT_EQ(Type::String, CreateColumnByType("LowCardinality(String)", create_column_settings)->As<ColumnString>()->GetType().GetCode());

    ASSERT_EQ(Type::FixedString, CreateColumnByType("LowCardinality(FixedString(10000))", create_column_settings)->GetType().GetCode());
    ASSERT_EQ(Type::FixedString, CreateColumnByType("LowCardinality(FixedString(10000))", create_column_settings)->As<ColumnFixedString>()->GetType().GetCode());
}

TEST(ColumnsCase, ArrayOfDecimal) {
    auto column = std::make_shared<clickhouse::ColumnDecimal>(18, 10);
    auto array = std::make_shared<clickhouse::ColumnArray>(column->Slice(0, 0));

    column->Append("1");
    column->Append("2");
    EXPECT_EQ(2u, column->Size());

    array->AppendAsColumn(column);
    ASSERT_EQ(1u, array->Size());
    EXPECT_EQ(2u, array->GetAsColumn(0)->Size());
}


class ColumnsCaseWithName : public ::testing::TestWithParam<const char* /*Column Type String*/>
{};

TEST_P(ColumnsCaseWithName, CreateColumnByType)
{
    const auto col = CreateColumnByType(GetParam());
    ASSERT_NE(nullptr, col);
    EXPECT_EQ(col->GetType().GetName(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Basic, ColumnsCaseWithName, ::testing::Values(
    "Int8", "Int16", "Int32", "Int64",
    "UInt8", "UInt16", "UInt32", "UInt64",
    "String", "Date", "DateTime",
    "UUID", "Int128"
));

INSTANTIATE_TEST_SUITE_P(Parametrized, ColumnsCaseWithName, ::testing::Values(
    "FixedString(0)", "FixedString(10000)",
    "DateTime('UTC')", "DateTime64(3, 'UTC')",
    "Decimal(9,3)", "Decimal(18,3)",
    "Enum8('ONE' = 1, 'TWO' = 2)",
    "Enum16('ONE' = 1, 'TWO' = 2, 'THREE' = 3, 'FOUR' = 4)"
));


INSTANTIATE_TEST_SUITE_P(Nested, ColumnsCaseWithName, ::testing::Values(
    "Nullable(FixedString(10000))",
    "Nullable(LowCardinality(FixedString(10000)))",
    "Array(Nullable(LowCardinality(FixedString(10000))))",
    "Array(Enum8('ONE' = 1, 'TWO' = 2))"
));
