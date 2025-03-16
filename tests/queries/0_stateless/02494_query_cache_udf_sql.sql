-- Tags: no-parallel
-- Tag no-parallel: Messes with internal cache

-- Test for issue #77553: SQL-defined UDFs may be non-deterministic. The query cache should treat them as such, i.e. reject them.
-- Also see 02494_query_cache_udf_executable.sh

SYSTEM DROP QUERY CACHE;
DROP FUNCTION IF EXISTS udf;

CREATE FUNCTION udf AS (a) -> a + 1;

SELECT udf(1) FORMAT Null SETTINGS use_query_cache = true, query_cache_nondeterministic_function_handling = 'throw'; -- { serverError QUERY_CACHE_USED_WITH_NONDETERMINISTIC_FUNCTIONS }
SELECT count(*) FROM system.query_cache;
SYSTEM DROP QUERY CACHE;

SELECT '-- query_cache_nondeterministic_function_handling = save';
SELECT udf(1) FORMAT Null SETTINGS use_query_cache = true, query_cache_nondeterministic_function_handling = 'save';
SELECT count(*) FROM system.query_cache;
SYSTEM DROP QUERY CACHE;

SELECT '-- query_cache_nondeterministic_function_handling = ignore';
SELECT udf(1) FORMAT Null SETTINGS use_query_cache = true, query_cache_nondeterministic_function_handling = 'ignore';
SELECT count(*) FROM system.query_cache;
SYSTEM DROP QUERY CACHE;

DROP FUNCTION udf;
