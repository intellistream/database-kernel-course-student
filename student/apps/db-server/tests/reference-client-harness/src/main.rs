//! Explicit differential harness: the tracked Rust ferrum-client drives the
//! independently built C++ server. CMake configures its path dependency only
//! when DB_REFERENCE_ROOT is supplied, so ordinary C++ tests never need
//! Cargo or the reference checkout.

use std::time::{Duration, Instant};

use db_tpcc_client::rmdb::{
    BatchOperation, BatchTerminal, PreparedStatement, RmdbClient, RmdbTerminal, SqlType, SqlValue,
};

const TIMEOUT: Duration = Duration::from_secs(5);
const MAX_RESPONSE_BYTES: usize = 1024 * 1024;

fn deadline() -> Instant {
    Instant::now() + TIMEOUT
}

fn command(client: &mut RmdbClient, sql: &str) -> Result<(), String> {
    let terminal = client
        .execute_discard(sql, deadline())
        .map_err(|error| format!("{sql}: {error}"))?;
    if !matches!(terminal, RmdbTerminal::CommandOk) {
        return Err(format!("{sql}: expected COMMAND_OK, received {terminal:?}"));
    }
    Ok(())
}

fn run(port: u16) -> Result<(), String> {
    let mut client = RmdbClient::connect("127.0.0.1", port, TIMEOUT, MAX_RESPONSE_BYTES)
        .map_err(|error| format!("connect: {error}"))?;

    command(
        &mut client,
        "CREATE TABLE official_client_items (id INT, label CHAR(16), amount FLOAT);",
    )?;
    command(
        &mut client,
        "CREATE UNIQUE INDEX official_client_items_pk ON official_client_items (id);",
    )?;
    command(&mut client, "CREATE INDEX official_client_items(label);")?;

    let statements = [
        PreparedStatement::command(1, "BEGIN;", vec![]),
        PreparedStatement::command(
            2,
            "INSERT INTO official_client_items VALUES($1,$2,$3);",
            vec![SqlType::Int32, SqlType::Char, SqlType::Float32],
        ),
        PreparedStatement::query(
            3,
            "SELECT id, label, amount FROM official_client_items WHERE id=$1;",
            vec![SqlType::Int32],
            vec![SqlType::Int32, SqlType::Char, SqlType::Float32],
        ),
        PreparedStatement::command(4, "COMMIT;", vec![]),
    ];
    let catalog = client
        .prepare_set(&statements, deadline())
        .map_err(|error| format!("PREPARE_SET: {error}"))?;
    let successful = client
        .execute_batch(
            &catalog,
            &[
                BatchOperation::new(1, vec![]),
                BatchOperation::new(
                    2,
                    vec![
                        SqlValue::Int32(1),
                        SqlValue::Char(b"official".to_vec()),
                        SqlValue::float(1.25),
                    ],
                ),
                BatchOperation::new(3, vec![SqlValue::Int32(1)]),
                BatchOperation::new(4, vec![]),
            ],
            deadline(),
        )
        .map_err(|error| format!("successful EXEC_BATCH: {error}"))?;
    if successful.terminal != BatchTerminal::Ok || successful.executed_operations != 4 {
        return Err(format!("successful EXEC_BATCH: {successful:?}"));
    }
    let result = successful.responses[2]
        .as_ref()
        .ok_or_else(|| "successful EXEC_BATCH omitted its query result".to_owned())?;
    if result.rows
        != vec![vec![
            SqlValue::Int32(1),
            SqlValue::Char(b"official".to_vec()),
            SqlValue::float(1.25),
        ]]
    {
        return Err(format!("prepared query rows differed: {:?}", result.rows));
    }

    let duplicate = client
        .execute_batch(
            &catalog,
            &[
                BatchOperation::new(1, vec![]),
                BatchOperation::new(
                    2,
                    vec![
                        SqlValue::Int32(2),
                        SqlValue::Char(b"first".to_vec()),
                        SqlValue::float(2.0),
                    ],
                ),
                BatchOperation::new(
                    2,
                    vec![
                        SqlValue::Int32(2),
                        SqlValue::Char(b"duplicate".to_vec()),
                        SqlValue::float(2.0),
                    ],
                ),
                BatchOperation::new(4, vec![]),
            ],
            deadline(),
        )
        .map_err(|error| format!("duplicate EXEC_BATCH: {error}"))?;
    if !matches!(
        duplicate.terminal,
        BatchTerminal::Error {
            failed_operation: 2,
            ..
        }
    ) {
        return Err(format!("duplicate EXEC_BATCH: {:?}", duplicate.terminal));
    }

    command(&mut client, "CREATE TABLE official_empty (id INT);")?;
    let sum = client
        .execute("SELECT SUM(id) FROM official_empty;", deadline())
        .map_err(|error| format!("empty SUM: {error}"))?;
    if sum.rows != vec![vec![SqlValue::Int32(0)]] {
        return Err(format!("empty SUM rows differed: {:?}", sum.rows));
    }
    command(&mut client, "CREATE STATIC_CHECKPOINT;")?;
    let load = client
        .execute_discard("LOAD /tmp/unsupported.csv INTO official_client_items;", deadline())
        .map_err(|error| format!("LOAD terminal: {error}"))?;
    if !matches!(load, RmdbTerminal::Error { .. }) {
        return Err(format!("LOAD must be rejected, received {load:?}"));
    }

    client.close();
    Ok(())
}

fn main() {
    let port = std::env::args()
        .nth(1)
        .ok_or_else(|| "missing RMDB port argument".to_owned())
        .and_then(|text| text.parse::<u16>().map_err(|error| error.to_string()))
        .and_then(run);
    if let Err(error) = port {
        eprintln!("reference client interoperability failure: {error}");
        std::process::exit(1);
    }
}
