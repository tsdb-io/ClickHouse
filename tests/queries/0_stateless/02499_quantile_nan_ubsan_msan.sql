SELECT quantiles(0.5)(now()) WHERE 0;
SELECT quantiles(0.5)(now()) WHERE 0 WITH TOTALS;
SELECT arrayReduce('quantiles(0.5)', []::Array(DateTime));
SELECT quantiles(0.5, 1.1754943508222875e-38, 0.0001, -0., 0.0001, -0., 0.0001, 0., 0.5)(now()) WHERE 0 WITH TOTALS;

SELECT DISTINCT arrayReduce('quantiles(0.5)', materialize([]::Array(DateTime))) FROM numbers(1000) LIMIT 10;
SELECT DISTINCT arrayReduce('quantiles(0, 0.5, 0.9, 1)', materialize([]::Array(DateTime))) FROM numbers(1000) LIMIT 10;
SELECT DISTINCT arrayReduce('quantiles(0.5)', [0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFE]) FROM numbers(1000) LIMIT 10;
SELECT DISTINCT arrayReduce('quantilesDeterministic(0.5)', materialize([]::Array(DateTime)), []::Array(UInt64)) FROM numbers(1000) LIMIT 10;
SELECT DISTINCT arrayReduce('quantilesDeterministic(0, 0.5, 0.9, 1)', materialize([]::Array(DateTime)), []::Array(UInt64)) FROM numbers(1000) LIMIT 10;
SELECT DISTINCT arrayReduce('quantiles(0.5)', [CAST(-1, 'UInt256'), CAST(-2, 'UInt256')]) FROM numbers(1000) LIMIT 10;
SELECT DISTINCT arrayReduce('quantiles(0.5)', []::Array(Float64)) FROM numbers(1000) LIMIT 10;
