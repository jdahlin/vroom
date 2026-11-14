-- The SQL corpus of bench/sqlite/run.py --target win64: run through clang's and
-- vcc's sqlite3.exe (a file database, then :memory:) and the outputs compared
-- byte for byte.  Deterministic: no random(), no time of day.
.bail off
.headers on
.mode list
.nullvalue NULL
PRAGMA page_size = 1024;
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- tables, indexes, constraints
CREATE TABLE dept(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, budget REAL CHECK(budget >= 0));
CREATE TABLE emp(id INTEGER PRIMARY KEY, dept INTEGER REFERENCES dept(id), name TEXT COLLATE NOCASE,
                 salary INTEGER, hired TEXT, note BLOB, UNIQUE(dept, name));
CREATE INDEX emp_salary ON emp(salary DESC, name);
CREATE INDEX emp_hired ON emp(substr(hired, 1, 4)) WHERE salary > 1000;
INSERT INTO dept VALUES (1, 'eng', 1.5e6), (2, 'ops', 250000.25), (3, 'art', 0), (4, 'empty', 1e-300);
WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 2000)
INSERT INTO emp(dept, name, salary, hired, note)
  SELECT 1 + i % 3, 'name' || printf('%05d', i * 7919 % 2003), (i * 37) % 5000 + 500,
         date('2000-01-01', '+' || (i * 13 % 9000) || ' days'), zeroblob(i % 17) || x'deadbeef'
  FROM n;
SELECT count(*), sum(salary), avg(salary), min(hired), max(hired), total(length(note)) FROM emp;
SELECT dept, count(*), sum(salary) FROM emp GROUP BY dept HAVING count(*) > 10 ORDER BY 1;
SELECT d.name, e.name, e.salary FROM dept d JOIN emp e ON e.dept = d.id WHERE e.salary > 5400 ORDER BY e.salary DESC, e.name LIMIT 12;
SELECT d.name, count(e.id) FROM dept d LEFT JOIN emp e ON e.dept = d.id GROUP BY d.id ORDER BY d.id;
SELECT name FROM emp WHERE name LIKE 'NAME0001%' ORDER BY name;
SELECT name FROM emp WHERE name GLOB 'name1[0-2]*' ORDER BY name LIMIT 5;
SELECT substr(hired, 1, 4) y, count(*) FROM emp WHERE salary > 1000 GROUP BY y ORDER BY y LIMIT 8;
EXPLAIN QUERY PLAN SELECT * FROM emp WHERE salary > 5000 ORDER BY salary DESC;

-- window functions, CTEs, upsert, returning
SELECT dept, name, salary, rank() OVER w, dense_rank() OVER w, percent_rank() OVER w,
       sum(salary) OVER (PARTITION BY dept ORDER BY salary ROWS BETWEEN 2 PRECEDING AND CURRENT ROW),
       lag(name) OVER w, lead(salary, 2, -1) OVER w, ntile(4) OVER w, cume_dist() OVER w
FROM emp WHERE id % 97 = 0 WINDOW w AS (PARTITION BY dept ORDER BY salary, name) ORDER BY dept, salary, name;
WITH RECURSIVE fib(a, b, k) AS (SELECT 0, 1, 0 UNION ALL SELECT b, a + b, k + 1 FROM fib WHERE k < 90)
SELECT k, a FROM fib WHERE k % 10 = 0;
WITH RECURSIVE m(x, y, it) AS (SELECT 0.0, 0.0, 0 UNION ALL
  SELECT x * x - y * y + -0.75, 2 * x * y + 0.1, it + 1 FROM m WHERE x * x + y * y < 4 AND it < 50)
SELECT it, printf('%.17g %.17g', x, y) FROM m WHERE it % 7 = 0;
CREATE TABLE kv(k TEXT PRIMARY KEY, v INTEGER, n INTEGER DEFAULT 0) WITHOUT ROWID;
INSERT INTO kv VALUES ('a', 1, 0), ('b', 2, 0);
INSERT INTO kv VALUES ('a', 10, 0), ('c', 3, 0) ON CONFLICT(k) DO UPDATE SET v = v + excluded.v, n = n + 1;
UPDATE kv SET v = v * 2 WHERE k > 'a' RETURNING k, v;
DELETE FROM kv WHERE k = 'c' RETURNING *;
SELECT * FROM kv ORDER BY k;

-- numbers: integer edges, real printing and parsing (SQLite's own printf and
-- its long double paths on this target), casts, math functions
SELECT 9223372036854775807, -9223372036854775808, 9223372036854775807 + 1, -9223372036854775808 - 1;
SELECT 9223372036854775807 * 2, abs(-9223372036854775807), 5 / 2, 5 % -3, -5 % 3, 5.0 / 2, 1 / 0, 1.0 / 0;
SELECT 0.1 + 0.2, 1e308 * 10, -1e308 * 10, 1e-320, 2.2250738585072014e-308, 4.9e-324, 1.7976931348623157e308;
SELECT printf('%.20e|%.17g|%f|%g|%!.25g|%.0f|%10.3f|%-10.2e|', 1.0 / 3, 0.1, 123456.789, 1e-5, 0.1, 2.5, -3.14159, 6.02214076e23);
SELECT printf('%d %5d %-5d| %x %X %o %c %s %q %Q %w', 42, 42, 42, 255, 255, 8, 'xyz', 'str', 'it''s', NULL, 'a"b');
SELECT printf('%lld %llu %,d %z', -9223372036854775808, -1, 1234567890, 'zz');
SELECT CAST('3.14abc' AS REAL), CAST('  12  ' AS INTEGER), CAST(1e19 AS INTEGER), CAST(-1e19 AS INTEGER), CAST(x'414243' AS TEXT);
SELECT '1e5' + 0, '0x1F' + 0, 0x1F, '9223372036854775808' + 0, 1.0e-5 = 0.00001, 3 = 3.0, typeof(3.0), typeof(1 << 62);
SELECT round(2.5), round(-2.5), round(1234.5678, 2), round(1e300, 2), round(0.49999999999999994);
SELECT sqrt(2), pow(2, 0.5), exp(1), ln(10), log10(1e-300), log2(3), sin(1), cos(1), tan(1), atan2(1, -1), pi();
SELECT ceil(-1.5), floor(-1.5), trunc(-1.5), mod(7.5, 2), degrees(pi()), radians(180), sign(-0.0), acosh(2), asinh(1), atanh(0.5);
SELECT 1e15 + 0.3, 1e16 + 1.0, 123456789012345678, 1.5e-7, 100.0, 1e100, -0.0, 0.000001234, 1e21, 123e-2;
SELECT sum(x), total(x), avg(x) FROM (SELECT 1e308 AS x UNION ALL SELECT 1e308 UNION ALL SELECT -1e308);
SELECT group_concat(printf('%.15g', i * 0.1), ' ') FROM (WITH RECURSIVE t(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM t WHERE i < 30) SELECT i FROM t);
SELECT median_like FROM (SELECT salary AS median_like FROM emp ORDER BY salary LIMIT 1 OFFSET 1000);

-- strings, blobs, collation, unicode
SELECT upper('straße'), lower('ÀÉÎ'), length('日本語'), octet_length('日本語'), unicode('€'), char(8364, 65), hex('é');
SELECT instr('hello world', 'o w'), replace('aaaa', 'aa', 'b'), trim('  x  '), ltrim('xxyxx', 'x'), rtrim('xxyxx', 'x');
SELECT quote(x'00ff'), hex(zeroblob(4)), length(randomblob(0)), quote(1.5), quote('a''b'), unhex('414243'), concat_ws('-', 1, NULL, 'b');
SELECT 'a' < 'B', 'a' < 'B' COLLATE NOCASE, 'abc' = 'ABC' COLLATE NOCASE, 'x ' = 'x' COLLATE RTRIM;
SELECT format('%s=%d', 'k', 7), soundex('Robert'), likelihood(1, 0.5), iif(1 > 2, 'y', 'n'), nullif(3, 3), coalesce(NULL, NULL, 'z');

-- date and time (fixed dates)
SELECT date('2024-02-28', '+1 day'), date('2023-02-28', '+1 day'), datetime(0, 'unixepoch'), julianday('2000-01-01 12:00:00');
SELECT strftime('%Y-%m-%d %H:%M:%f %j %w %s', '2026-11-14 01:02:03.456'), time('12:34:56', '+3723 seconds');
SELECT datetime('2026-11-14', 'start of month', '+1 month', '-1 day'), unixepoch('2038-01-19 03:14:08'), timediff('2026-11-14', '2000-02-29');

-- JSON
SELECT json('{"a":1,"b":[1,2,{"c":null}],"d":1.5e300}'), json_extract('{"a":{"b":[10,20]}}', '$.a.b[1]');
SELECT json_array(1, 2.5, 'x', NULL, json('[1]')), json_object('k', 1, 'v', json_array()), json_valid('{"x":}');
SELECT key, value, type FROM json_each('{"x":1,"y":[2,3],"z":{"w":0.1}}');
SELECT json_group_array(name) FROM (SELECT name FROM emp ORDER BY id LIMIT 5);
SELECT json_set('{"a":1}', '$.b', 2, '$.a', 3), json_remove('[1,2,3]', '$[1]'), json_patch('{"a":1,"b":2}', '{"b":null,"c":3}');
SELECT fullkey, atom FROM json_tree('[1,[2,[3,[4.25e-10]]]]') WHERE atom IS NOT NULL;

-- full-text search and r-tree
CREATE VIRTUAL TABLE doc USING fts5(title, body);
INSERT INTO doc VALUES ('alpha', 'the quick brown fox jumps over the lazy dog'),
  ('beta', 'sqlite is a small fast reliable sql database engine'),
  ('gamma', 'windows is an operating system; the fox is quick'),
  ('delta', 'compilers turn source code into machine code quickly');
SELECT title, highlight(doc, 1, '[', ']') FROM doc WHERE doc MATCH 'quick OR fox' ORDER BY rank, title;
SELECT title, snippet(doc, 1, '<', '>', '...', 4) FROM doc WHERE doc MATCH 'code NEAR machine' ORDER BY title;
SELECT title, round(bm25(doc), 6) FROM doc WHERE doc MATCH 'the' ORDER BY title;
CREATE VIRTUAL TABLE box USING rtree(id, x0, x1, y0, y1);
WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 300)
INSERT INTO box SELECT i, i % 50, i % 50 + 3.5, i / 10.0, i / 10.0 + 1.25 FROM n;
SELECT count(*), min(id), max(id) FROM box WHERE x0 <= 20 AND x1 >= 18 AND y0 <= 15 AND y1 >= 14;

-- triggers, views, transactions, savepoints
CREATE TABLE log(t TEXT);
CREATE TRIGGER emp_raise AFTER UPDATE OF salary ON emp WHEN new.salary > old.salary * 1.5
BEGIN INSERT INTO log VALUES (printf('%s %d->%d', old.name, old.salary, new.salary)); END;
CREATE VIEW rich AS SELECT name, salary FROM emp WHERE salary > 5450;
BEGIN;
UPDATE emp SET salary = salary * 2 WHERE id % 211 = 0;
SAVEPOINT s1;
DELETE FROM emp WHERE id < 100;
ROLLBACK TO s1;
RELEASE s1;
COMMIT;
SELECT * FROM log ORDER BY t;
SELECT count(*) FROM emp;
SELECT * FROM rich ORDER BY salary DESC, name LIMIT 6;
BEGIN; DELETE FROM emp WHERE dept = 3; ROLLBACK;
SELECT count(*) FROM emp WHERE dept = 3;
INSERT INTO emp(dept, name) VALUES (9, 'nobody');
SELECT changes(), total_changes() > 0;

-- generated columns, strict tables, attach
CREATE TABLE g(a INTEGER, b INTEGER, c AS (a * b) STORED, d AS (a || '-' || b) VIRTUAL) STRICT;
INSERT INTO g(a, b) VALUES (3, 4), (-2, 7);
SELECT * FROM g ORDER BY a;
INSERT INTO g(a, b) VALUES ('x', 1);
ATTACH ':memory:' AS aux;
CREATE TABLE aux.t(x);
INSERT INTO aux.t SELECT salary FROM emp WHERE id <= 5;
SELECT sum(x) FROM aux.t;
DETACH aux;

-- integrity, maintenance, the schema
PRAGMA integrity_check;
PRAGMA foreign_key_check;
ANALYZE;
SELECT tbl, idx FROM sqlite_stat1 ORDER BY tbl, idx;
VACUUM;
PRAGMA wal_checkpoint(TRUNCATE);
SELECT type, name, tbl_name FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%' AND name NOT LIKE 'doc_%' AND name NOT LIKE 'box_%' ORDER BY type, name;

-- output modes of the shell
.mode box
SELECT id, name, budget FROM dept ORDER BY id;
.mode markdown
SELECT k, v, n FROM kv ORDER BY k;
.mode json
SELECT id, name, salary, hex(note) AS note FROM emp ORDER BY id LIMIT 3;
.mode csv
SELECT 'a,b', 'c"d', 1.25, NULL;
.mode quote
SELECT 'x', 1, 2.5, x'0102', NULL;
.mode insert t2
SELECT 'it''s', 1e10;
.mode line
SELECT * FROM g WHERE a = 3;
.mode table
SELECT dept, count(*) n FROM emp GROUP BY dept ORDER BY dept;
.mode list
.schema dept
.dump kv
.tables
