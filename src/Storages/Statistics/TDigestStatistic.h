#pragma once

#include <Storages/Statistics/Statistics.h>
#include <AggregateFunctions/QuantileTDigest.h>

namespace DB
{


/// TDigestStatistic is a kind of histogram.
class TDigestStatistic : public IStatistic
{
    friend class ColumnStatistics;
    QuantileTDigest<Float64> data;
public:
    explicit TDigestStatistic(const StatisticDescription & stat_) : IStatistic(stat_)
    {
    }

    Float64 estimateLess(Float64 val) const;

    Float64 estimateEqual(Float64 val) const;

    void serialize(WriteBuffer & buf) override;

    void deserialize(ReadBuffer & buf) override;

    void update(const ColumnPtr & column) override;
};

}
