#!/usr/bin/env python3
"""
Generate a TPC-C-like workload file using BenchmarkSQL + PostgreSQL.

What you get at the end:
- workload.txt
- one integer per line
- values are consecutive and unique: 0, 1, 2, ..., N-1
- output order is based on TPC-C order timestamps, so it is usually mostly
  increasing with some natural interleaving/disorder

Assumptions:
- PostgreSQL server is already installed and running
- git, bash, and mvn are installed
- Python package psycopg is installed:
    pip install "psycopg[binary]"
"""

from __future__ import annotations

import math
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import psycopg
from psycopg import sql

# Ensure locally-installed Maven is on PATH
_maven_bin = Path.home() / "maven" / "bin"
if _maven_bin.exists():
    os.environ["PATH"] = str(_maven_bin) + os.pathsep + os.environ.get("PATH", "")

# Ensure a JDK (not just JRE) is available for Maven compilation
_jdk_candidates = [
    Path("/scratch2/karatse/.jdks/openjdk-24.0.1"),
]
if not os.environ.get("JAVA_HOME"):
    for _jdk in _jdk_candidates:
        if (_jdk / "bin" / "javac").exists():
            os.environ["JAVA_HOME"] = str(_jdk)
            os.environ["PATH"] = str(_jdk / "bin") + os.pathsep + os.environ.get("PATH", "")
            break


# =========================
# Configuration
# =========================

# Where to put the repo and outputs
WORKDIR = Path.cwd() / "benchmarksql_workdir"
BENCHMARKSQL_REPO = "https://github.com/pgsql-io/benchmarksql.git"
BENCHMARKSQL_DIR = WORKDIR / "benchmarksql"

# PostgreSQL admin connection, used only to create DB/user if needed
PG_ADMIN_HOST = "127.0.0.1"
PG_ADMIN_PORT = 5432
PG_ADMIN_DB = "postgres"
PG_ADMIN_USER = "cgokmen"
PG_ADMIN_PASSWORD = ""   # peer/trust auth — no password needed

# BenchmarkSQL app database/user
APP_DB = "benchmarksql"
APP_USER = "benchmarksql"
APP_PASSWORD = "benchmarksql"    # change this

# Final workload
TARGET_ROWS = 100_000_000        # final number of integers you want
OUTPUT_FILE = WORKDIR / "workload.txt"

# Scale and runtime
# TPC-C starts with about 30,000 ORDER rows per warehouse, so this chooses enough
# warehouses to have at least TARGET_ROWS rows available even before the timed run.
AUTO_WAREHOUSES = True
WAREHOUSES = 1                   # ignored if AUTO_WAREHOUSES = True

# A short run is enough to create a workload file.
RUN_MINS = 1
RAMPUP_MINS = 1
RAMPUP_SUT_MINS = 0
RAMPUP_TERMINAL_MINS = 0

# Small machine defaults
LOAD_WORKERS = 4
MONKEYS = 4
SUT_THREADS = 8
MAX_DELIVERY_BG_THREADS = 4
MAX_DELIVERY_BG_PER_WAREHOUSE = 1

# These speed multipliers are not for publishing official results.
# They just help generate data faster.
KEYING_TIME_MULTIPLIER = 0.1
THINK_TIME_MULTIPLIER = 0.1

# Benchmark mix weights
PAYMENT_WEIGHT = 43.1
ORDER_STATUS_WEIGHT = 4.1
STOCK_LEVEL_WEIGHT = 4.1
DELIVERY_WEIGHT = 4.1

# Git clone behavior
CLONE_IF_MISSING = True


# =========================
# Helpers
# =========================

@dataclass
class PgConnInfo:
    host: str
    port: int
    dbname: str
    user: str
    password: str

    def dsn(self) -> str:
        parts = (
            f"host={self.host} port={self.port} dbname={self.dbname} "
            f"user={self.user}"
        )
        if self.password:
            parts += f" password={self.password}"
        return parts


def run(cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None, check: bool = True) -> None:
    print(f"\n[RUN] {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd, env=env, check=check)


def ensure_tools() -> None:
    needed = ["git", "bash", "mvn"]
    missing = [tool for tool in needed if shutil.which(tool) is None]
    if missing:
        raise RuntimeError(f"Missing required tools: {', '.join(missing)}")


def ensure_repo() -> None:
    WORKDIR.mkdir(parents=True, exist_ok=True)
    if BENCHMARKSQL_DIR.exists():
        print(f"[INFO] Reusing existing repo: {BENCHMARKSQL_DIR}")
        return
    if not CLONE_IF_MISSING:
        raise RuntimeError(f"Repo not found at {BENCHMARKSQL_DIR}")
    run(["git", "clone", BENCHMARKSQL_REPO, str(BENCHMARKSQL_DIR)])


def build_benchmarksql() -> Path:
    # BenchmarkSQL docs say to build with Maven and use the generated target/run directory.
    run(["mvn"], cwd=BENCHMARKSQL_DIR)
    run_dir = BENCHMARKSQL_DIR / "target" / "run"
    if not run_dir.exists():
        raise RuntimeError(f"Expected run directory not found: {run_dir}")
    return run_dir


def ensure_db_and_user(admin: PgConnInfo, app: PgConnInfo) -> None:
    print("[INFO] Ensuring PostgreSQL database and user exist...")
    with psycopg.connect(admin.dsn(), autocommit=True) as conn:
        with conn.cursor() as cur:
            cur.execute("SELECT 1 FROM pg_roles WHERE rolname = %s", (app.user,))
            if cur.fetchone() is None:
                cur.execute(
                    sql.SQL("CREATE USER {} WITH PASSWORD {}").format(
                        sql.Identifier(app.user),
                        sql.Literal(app.password),
                    )
                )
                print(f"[INFO] Created role {app.user}")
            else:
                print(f"[INFO] Role {app.user} already exists")

            cur.execute("SELECT 1 FROM pg_database WHERE datname = %s", (app.dbname,))
            if cur.fetchone() is None:
                cur.execute(
                    sql.SQL("CREATE DATABASE {} OWNER {}")
                    .format(sql.Identifier(app.dbname), sql.Identifier(app.user))
                )
                print(f"[INFO] Created database {app.dbname}")
            else:
                print(f"[INFO] Database {app.dbname} already exists")


def find_sample_properties(run_dir: Path) -> Path:
    candidates = (
        sorted(run_dir.glob("sample*postgres*.properties")) +
        sorted(run_dir.glob("sample*Postgres*.properties")) +
        sorted(run_dir.glob("sample*.properties"))
    )
    if not candidates:
        raise RuntimeError(f"No sample properties file found under {run_dir}")
    return candidates[0]


def patch_properties_file(src: Path, dst: Path, updates: dict[str, str]) -> None:
    text = src.read_text(encoding="utf-8")
    lines = text.splitlines()
    seen = set()

    def set_or_replace(key: str, value: str) -> None:
        nonlocal lines
        pattern = re.compile(rf"^\s*{re.escape(key)}\s*=.*$")
        replaced = False
        for i, line in enumerate(lines):
            if pattern.match(line):
                lines[i] = f"{key}={value}"
                replaced = True
                break
        if not replaced:
            lines.append(f"{key}={value}")
        seen.add(key)

    for k, v in updates.items():
        set_or_replace(k, v)

    dst.write_text("\n".join(lines) + "\n", encoding="utf-8")


def maybe_destroy_existing_schema(run_dir: Path, props_file: Path) -> None:
    destroy_script = run_dir / "runDatabaseDestroy.sh"
    if destroy_script.exists():
        try:
            run(["bash", str(destroy_script), props_file.name], cwd=run_dir, check=False)
        except Exception:
            pass


def run_build_and_benchmark(run_dir: Path, props_file: Path) -> None:
    run(["bash", "runDatabaseBuild.sh", props_file.name], cwd=run_dir)
    run(["bash", "runBenchmark.sh", props_file.name], cwd=run_dir)


def find_order_table(conn: psycopg.Connection) -> tuple[str, str]:
    """
    Find the schema/table that looks like the ORDER table by required columns.
    """
    q = """
    SELECT table_schema, table_name
    FROM information_schema.columns
    WHERE table_schema NOT IN ('pg_catalog', 'information_schema')
      AND column_name IN ('o_id', 'o_w_id', 'o_d_id', 'o_entry_d')
    GROUP BY table_schema, table_name
    HAVING COUNT(DISTINCT column_name) = 4
    ORDER BY table_schema, table_name
    LIMIT 1
    """
    with conn.cursor() as cur:
        cur.execute(q)
        row = cur.fetchone()
    if row is None:
        raise RuntimeError("Could not find an ORDER table with columns o_id, o_w_id, o_d_id, o_entry_d")
    schema_name, table_name = row
    print(f"[INFO] Using order table: {schema_name}.{table_name}")
    return schema_name, table_name


def count_available_rows(conn: psycopg.Connection, schema_name: str, table_name: str) -> int:
    q = sql.SQL("SELECT COUNT(*) FROM {}.{}").format(
        sql.Identifier(schema_name),
        sql.Identifier(table_name),
    )
    with conn.cursor() as cur:
        cur.execute(q)
        return int(cur.fetchone()[0])


def export_workload(
    conn: psycopg.Connection,
    schema_name: str,
    table_name: str,
    target_rows: int,
    outfile: Path,
) -> int:
    """
    Export a workload file of consecutive unique integers:
    - rank rows by (o_id, o_w_id, o_d_id) to get 0..N-1
    - emit those integers in o_entry_d order to keep TPC-C time interleaving
    """
    outfile.parent.mkdir(parents=True, exist_ok=True)

    copy_query = sql.SQL(
        """
        COPY (
            WITH ranked AS (
                SELECT
                    ROW_NUMBER() OVER (ORDER BY o_id, o_w_id, o_d_id) - 1 AS key_int,
                    o_entry_d,
                    o_w_id,
                    o_d_id,
                    o_id
                FROM {}.{}
            )
            SELECT key_int
            FROM ranked
            ORDER BY o_entry_d, o_w_id, o_d_id, o_id
            LIMIT %s
        ) TO STDOUT
        """
    ).format(
        sql.Identifier(schema_name),
        sql.Identifier(table_name),
    )

    with conn.cursor() as cur, open(outfile, "wb") as f:
        with cur.copy(copy_query, (target_rows,)) as copy:
            for data in copy:
                f.write(data)

    return target_rows


def main() -> None:
    ensure_tools()
    ensure_repo()
    run_dir = build_benchmarksql()

    admin = PgConnInfo(
        host=PG_ADMIN_HOST,
        port=PG_ADMIN_PORT,
        dbname=PG_ADMIN_DB,
        user=PG_ADMIN_USER,
        password=PG_ADMIN_PASSWORD,
    )
    app = PgConnInfo(
        host=PG_ADMIN_HOST,
        port=PG_ADMIN_PORT,
        dbname=APP_DB,
        user=APP_USER,
        password=APP_PASSWORD,
    )

    ensure_db_and_user(admin, app)

    warehouses = WAREHOUSES
    if AUTO_WAREHOUSES:
        warehouses = max(1, math.ceil(TARGET_ROWS / 30000))
    print(f"[INFO] warehouses={warehouses}")

    sample_props = find_sample_properties(run_dir)
    my_props = run_dir / "my.properties"

    props_updates = {
        "db": "postgres",
        "driver": "org.postgresql.Driver",
        "application": "Generic",
        "conn": f"jdbc:postgresql://{app.host}:{app.port}/{app.dbname}",
        "user": app.user,
        "password": app.password,

        "warehouses": str(warehouses),
        "loadWorkers": str(LOAD_WORKERS),
        "monkeys": str(MONKEYS),
        "sutThreads": str(SUT_THREADS),
        "maxDeliveryBGThreads": str(MAX_DELIVERY_BG_THREADS),
        "maxDeliveryBGPerWarehouse": str(MAX_DELIVERY_BG_PER_WAREHOUSE),

        "rampupMins": str(RAMPUP_MINS),
        "rampupSUTMins": str(RAMPUP_SUT_MINS),
        "rampupTerminalMins": str(RAMPUP_TERMINAL_MINS),
        "runMins": str(RUN_MINS),
        "runTxnsPerTerminal": "0",
        "reportIntervalSecs": "60",

        "keyingTimeMultiplier": str(KEYING_TIME_MULTIPLIER),
        "thinkTimeMultiplier": str(THINK_TIME_MULTIPLIER),

        "paymentWeight": str(PAYMENT_WEIGHT),
        "orderStatusWeight": str(ORDER_STATUS_WEIGHT),
        "stockLevelWeight": str(STOCK_LEVEL_WEIGHT),
        "deliveryWeight": str(DELIVERY_WEIGHT),
    }

    patch_properties_file(sample_props, my_props, props_updates)
    print(f"[INFO] Wrote properties file: {my_props}")

    maybe_destroy_existing_schema(run_dir, my_props)
    run_build_and_benchmark(run_dir, my_props)

    with psycopg.connect(app.dsn(), autocommit=True) as conn:
        schema_name, table_name = find_order_table(conn)
        available = count_available_rows(conn, schema_name, table_name)
        export_n = min(TARGET_ROWS, available)

        if export_n < TARGET_ROWS:
            print(
                f"[WARN] Requested {TARGET_ROWS:,} rows but only {available:,} are available. "
                f"Exporting {export_n:,} rows."
            )

        export_workload(conn, schema_name, table_name, export_n, OUTPUT_FILE)

    print(f"\nDone: {OUTPUT_FILE}")
    print(f"Rows written: {export_n:,}")
    print("Format: one integer per line, no header")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"\n[ERROR] {e}", file=sys.stderr)
        sys.exit(1)