SET send_logs_level = 'fatal';
SET enable_analyzer = 0;
SET allow_experimental_window_view = 1;
DROP DATABASE IF EXISTS {CLICKHOUSE_DATABASE:Identifier};
set allow_deprecated_database_ordinary=1;
-- Creation of a database with Ordinary engine emits a warning.
CREATE DATABASE {CLICKHOUSE_DATABASE:Identifier} ENGINE=Ordinary;

DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.mt;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.mt_2;

CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.mt(a Int32, b Int32, timestamp DateTime) ENGINE=MergeTree ORDER BY tuple();
CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.mt_2(a Int32, b Int32, timestamp DateTime) ENGINE=MergeTree ORDER BY tuple();

SELECT '---TUMBLE---';
SELECT '||---WINDOW COLUMN NAME---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY tumble(timestamp, INTERVAL '1' SECOND) ENGINE Memory AS SELECT count(a), tumbleEnd(wid) AS count FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY tumble(timestamp, INTERVAL '1' SECOND) as wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---WINDOW COLUMN ALIAS---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY wid ENGINE Memory AS SELECT count(a) AS count, tumble(timestamp, INTERVAL '1' SECOND) AS wid FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---DATA COLUMN ALIAS---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY id ENGINE Memory AS SELECT count(a) AS count, b as id FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY id, tumble(timestamp, INTERVAL '1' SECOND);
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---IDENTIFIER---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY (tumble(timestamp, INTERVAL '1' SECOND), b) PRIMARY KEY tumble(timestamp, INTERVAL '1' SECOND) ENGINE Memory AS SELECT count(a) AS count FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY b, tumble(timestamp, INTERVAL '1' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---FUNCTION---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY (tumble(timestamp, INTERVAL '1' SECOND), plus(a, b)) PRIMARY KEY tumble(timestamp, INTERVAL '1' SECOND) ENGINE Memory AS SELECT count(a) AS count FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY plus(a, b) as _type, tumble(timestamp, INTERVAL '1' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---PARTITION---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY wid PARTITION BY wid ENGINE Memory AS SELECT count(a) AS count, tumble(now(), INTERVAL '1' SECOND) AS wid FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---JOIN---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY tumble({CLICKHOUSE_DATABASE:Identifier}.mt.timestamp, INTERVAL '1' SECOND) ENGINE Memory AS SELECT count({CLICKHOUSE_DATABASE:Identifier}.mt.a), count({CLICKHOUSE_DATABASE:Identifier}.mt_2.b), wid FROM {CLICKHOUSE_DATABASE:Identifier}.mt JOIN {CLICKHOUSE_DATABASE:Identifier}.mt_2 ON {CLICKHOUSE_DATABASE:Identifier}.mt.timestamp = {CLICKHOUSE_DATABASE:Identifier}.mt_2.timestamp GROUP BY tumble({CLICKHOUSE_DATABASE:Identifier}.mt.timestamp, INTERVAL '1' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY wid ENGINE Memory AS SELECT count({CLICKHOUSE_DATABASE:Identifier}.mt.a), count({CLICKHOUSE_DATABASE:Identifier}.mt_2.b), wid FROM {CLICKHOUSE_DATABASE:Identifier}.mt JOIN {CLICKHOUSE_DATABASE:Identifier}.mt_2 ON {CLICKHOUSE_DATABASE:Identifier}.mt.timestamp = {CLICKHOUSE_DATABASE:Identifier}.mt_2.timestamp GROUP BY tumble({CLICKHOUSE_DATABASE:Identifier}.mt.timestamp, INTERVAL '1' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;


SELECT '---HOP---';
SELECT '||---WINDOW COLUMN NAME---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) ENGINE Memory AS SELECT count(a) AS count, hopEnd(wid) FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) as wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---WINDOW COLUMN ALIAS---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY wid ENGINE Memory AS SELECT count(a) AS count, hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) AS wid FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---DATA COLUMN ALIAS---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY id ENGINE Memory AS SELECT count(a) AS count, b as id FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY id, hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND);
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---IDENTIFIER---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY (hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND), b) PRIMARY KEY hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) ENGINE Memory AS SELECT count(a) AS count FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY b, hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---FUNCTION---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY (hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND), plus(a, b)) PRIMARY KEY hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) ENGINE Memory AS SELECT count(a) AS count FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY plus(a, b) as _type, hop(timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---PARTITION---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY wid PARTITION BY wid ENGINE Memory AS SELECT count(a) AS count, hopEnd(wid) FROM {CLICKHOUSE_DATABASE:Identifier}.mt GROUP BY hop(now(), INTERVAL '1' SECOND, INTERVAL '3' SECOND) as wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

SELECT '||---JOIN---';
DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY hop({CLICKHOUSE_DATABASE:Identifier}.mt.timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) ENGINE Memory AS SELECT count({CLICKHOUSE_DATABASE:Identifier}.mt.a), count({CLICKHOUSE_DATABASE:Identifier}.mt_2.b), wid FROM {CLICKHOUSE_DATABASE:Identifier}.mt JOIN {CLICKHOUSE_DATABASE:Identifier}.mt_2 ON {CLICKHOUSE_DATABASE:Identifier}.mt.timestamp = {CLICKHOUSE_DATABASE:Identifier}.mt_2.timestamp GROUP BY hop({CLICKHOUSE_DATABASE:Identifier}.mt.timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

DROP TABLE IF EXISTS {CLICKHOUSE_DATABASE:Identifier}.wv;
CREATE WINDOW VIEW {CLICKHOUSE_DATABASE:Identifier}.wv INNER ENGINE AggregatingMergeTree ORDER BY wid ENGINE Memory AS SELECT count({CLICKHOUSE_DATABASE:Identifier}.mt.a), count({CLICKHOUSE_DATABASE:Identifier}.mt_2.b), wid FROM {CLICKHOUSE_DATABASE:Identifier}.mt JOIN {CLICKHOUSE_DATABASE:Identifier}.mt_2 ON {CLICKHOUSE_DATABASE:Identifier}.mt.timestamp = {CLICKHOUSE_DATABASE:Identifier}.mt_2.timestamp GROUP BY hop({CLICKHOUSE_DATABASE:Identifier}.mt.timestamp, INTERVAL '1' SECOND, INTERVAL '3' SECOND) AS wid;
SHOW CREATE TABLE {CLICKHOUSE_DATABASE:Identifier}.`.inner.wv`;

DROP TABLE {CLICKHOUSE_DATABASE:Identifier}.wv;
DROP TABLE {CLICKHOUSE_DATABASE:Identifier}.mt;
DROP TABLE {CLICKHOUSE_DATABASE:Identifier}.mt_2;
