#include <Storages/Statistics/Statistics.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>
#include <Storages/ColumnsDescription.h>
#include <Storages/Statistics/ConditionSelectivityEstimator.h>
#include <Storages/Statistics/StatisticsCountMinSketch.h>
#include <Storages/Statistics/StatisticsTDigest.h>
#include <Storages/Statistics/StatisticsUniq.h>
#include <Storages/StatisticsDescription.h>
#include <Common/Exception.h>
#include <Common/logger_useful.h>


#include "config.h" /// USE_DATASKETCHES

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
    extern const int INCORRECT_QUERY;
}

enum StatisticsFileVersion : UInt16
{
    V0 = 0,
};

std::optional<Float64> StatisticsUtils::tryConvertToFloat64(const Field & field)
{
    switch (field.getType())
    {
        case Field::Types::Int64:
            return field.get<Int64>();
        case Field::Types::UInt64:
            return field.get<UInt64>();
        case Field::Types::Float64:
            return field.get<Float64>();
        case Field::Types::Int128:
            return field.get<Int128>();
        case Field::Types::UInt128:
            return field.get<UInt128>();
        case Field::Types::Int256:
            return field.get<Int256>();
        case Field::Types::UInt256:
            return field.get<UInt256>();
        default:
            return {};
    }
}

std::optional<String> StatisticsUtils::tryConvertToString(const DB::Field & field)
{
    if (field.getType() == Field::Types::String)
        return field.get<String>();
    return {};
}

IStatistics::IStatistics(const SingleStatisticsDescription & stat_)
    : stat(stat_)
{
}

ColumnStatistics::ColumnStatistics(const ColumnStatisticsDescription & stats_desc_)
    : stats_desc(stats_desc_)
{
}

void ColumnStatistics::update(const ColumnPtr & column)
{
    rows += column->size();
    for (const auto & stat : stats)
        stat.second->update(column);
}

UInt64 IStatistics::estimateCardinality() const
{
    throw Exception(ErrorCodes::LOGICAL_ERROR, "Cardinality estimation is not implemented for this type of statistics");
}

Float64 IStatistics::estimateEqual(const Field & /*val*/) const
{
    throw Exception(ErrorCodes::LOGICAL_ERROR, "Equality estimation is not implemented for this type of statistics");
}

Float64 IStatistics::estimateLess(const Field & /*val*/) const
{
    throw Exception(ErrorCodes::LOGICAL_ERROR, "Less-than estimation is not implemented for this type of statistics");
}

/// -------------------------------------
/// Implementation of the estimation:
/// Note: Each statistics object supports certain types predicates natively, e.g.
/// - TDigest: '< X' (less-than predicates)
/// - Count-min sketches: '= X' (equal predicates)
/// - Uniq (HyperLogLog): 'count distinct(*)' (column cardinality)
/// If multiple statistics objects are available per column, it is sometimes also possible to combine them in a clever way.
/// For that reason, all estimation are performed in a central place (here), and we don't simply pass the predicate to the first statistics
/// object that supports it natively.

Float64 ColumnStatistics::estimateLess(const Field & val) const
{
    if (stats.contains(StatisticsType::TDigest))
        return stats.at(StatisticsType::TDigest)->estimateLess(val);
    return rows * ConditionSelectivityEstimator::default_normal_cond_factor;
}

Float64 ColumnStatistics::estimateGreater(const Field & val) const
{
    return rows - estimateLess(val);
}

Float64 ColumnStatistics::estimateEqual(const Field & val) const
{
    auto float_val = StatisticsUtils::tryConvertToFloat64(val);
    if (float_val.has_value() && stats.contains(StatisticsType::Uniq) && stats.contains(StatisticsType::TDigest))
    {
        /// 2048 is the default number of buckets in TDigest. In this case, TDigest stores exactly one value (with many rows) for every bucket.
        if (stats.at(StatisticsType::Uniq)->estimateCardinality() < 2048)
            return stats.at(StatisticsType::TDigest)->estimateEqual(val);
    }
#if USE_DATASKETCHES
    if (stats.contains(StatisticsType::CountMinSketch))
        return stats.at(StatisticsType::CountMinSketch)->estimateEqual(val);
#endif
    if (!float_val.has_value() && (float_val < - ConditionSelectivityEstimator::threshold || float_val > ConditionSelectivityEstimator::threshold))
        return rows * ConditionSelectivityEstimator::default_normal_cond_factor;
    else
        return rows * ConditionSelectivityEstimator::default_good_cond_factor;
}

/// -------------------------------------

void ColumnStatistics::serialize(WriteBuffer & buf)
{
    writeIntBinary(V0, buf);

    UInt64 stat_types_mask = 0;
    for (const auto & [type, _]: stats)
        stat_types_mask |= 1 << UInt8(type);
    writeIntBinary(stat_types_mask, buf);

    /// as the column row count is always useful, save it in any case
    writeIntBinary(rows, buf);

    /// write the actual statistics object
    for (const auto & [type, stat_ptr] : stats)
        stat_ptr->serialize(buf);
}

void ColumnStatistics::deserialize(ReadBuffer &buf)
{
    UInt16 version;
    readIntBinary(version, buf);
    if (version != V0)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown file format version: {}", version);

    UInt64 stat_types_mask = 0;
    readIntBinary(stat_types_mask, buf);

    readIntBinary(rows, buf);

    for (auto it = stats.begin(); it != stats.end();)
    {
        if (!(stat_types_mask & 1 << UInt8(it->first)))
        {
            stats.erase(it++);
        }
        else
        {
            it->second->deserialize(buf);
            ++it;
        }
    }
}

String ColumnStatistics::getFileName() const
{
    return STATS_FILE_PREFIX + columnName();
}

const String & ColumnStatistics::columnName() const
{
    return stats_desc.column_name;
}

UInt64 ColumnStatistics::rowCount() const
{
    return rows;
}

void MergeTreeStatisticsFactory::registerCreator(StatisticsType stats_type, Creator creator)
{
    if (!creators.emplace(stats_type, std::move(creator)).second)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "MergeTreeStatisticsFactory: the statistics creator type {} is not unique", stats_type);
}

void MergeTreeStatisticsFactory::registerValidator(StatisticsType stats_type, Validator validator)
{
    if (!validators.emplace(stats_type, std::move(validator)).second)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "MergeTreeStatisticsFactory: the statistics validator type {} is not unique", stats_type);
}

MergeTreeStatisticsFactory::MergeTreeStatisticsFactory()
{
    registerValidator(StatisticsType::TDigest, tdigestValidator);
    registerCreator(StatisticsType::TDigest, tdigestCreator);

    registerValidator(StatisticsType::Uniq, uniqValidator);
    registerCreator(StatisticsType::Uniq, uniqCreator);

#if USE_DATASKETCHES
    registerValidator(StatisticsType::CountMinSketch, countMinSketchValidator);
    registerCreator(StatisticsType::CountMinSketch, countMinSketchCreator);
#endif
}

MergeTreeStatisticsFactory & MergeTreeStatisticsFactory::instance()
{
    static MergeTreeStatisticsFactory instance;
    return instance;
}

void MergeTreeStatisticsFactory::validate(const ColumnStatisticsDescription & stats, DataTypePtr data_type) const
{
    for (const auto & [type, desc] : stats.types_to_desc)
    {
        auto it = validators.find(type);
        if (it == validators.end())
            throw Exception(ErrorCodes::LOGICAL_ERROR, "Unknown statistic type '{}'", type);
        it->second(desc, data_type);
    }
}

ColumnStatisticsPtr MergeTreeStatisticsFactory::get(const ColumnStatisticsDescription & stats) const
{
    ColumnStatisticsPtr column_stat = std::make_shared<ColumnStatistics>(stats);
    for (const auto & [type, desc] : stats.types_to_desc)
    {
        auto it = creators.find(type);
        if (it == creators.end())
            throw Exception(ErrorCodes::INCORRECT_QUERY, "Unknown statistic type '{}'. Available types: 'tdigest' 'uniq' and 'count_min'", type);
        auto stat_ptr = (it->second)(desc, stats.data_type);
        column_stat->stats[type] = stat_ptr;
    }
    return column_stat;
}

ColumnsStatistics MergeTreeStatisticsFactory::getMany(const ColumnsDescription & columns) const
{
    ColumnsStatistics result;
    for (const auto & col : columns)
        if (!col.statistics.empty())
            result.push_back(get(col.statistics));
    return result;
}

}
