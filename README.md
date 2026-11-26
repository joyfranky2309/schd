# schd

`schd` is a command-line scheduler that runs jobs defined in a JSON file.

## Quick start

No SQLite setup is needed before launching because `./run.sh` builds SQLite support automatically:

```sh
sudo apt-get install -y libsqlite3-dev
./run.sh validate examples/etl.json
./run.sh run examples/etl.json
./run.sh run examples/etl.json --db my.db
./run.sh history my.db --limit 5
```

With no arguments, `./run.sh` runs `examples/pricing.json`:

```sh
./run.sh
```

Set `SCHD_BUILD_DIR` to choose another build directory:

```sh
SCHD_BUILD_DIR=build-custom ./run.sh validate examples/etl.json
```

## Using job files

Create a JSON file like `jobs.json`, then run it:

```sh
./run.sh validate jobs.json
./run.sh run jobs.json --db my.db
```

Example `jobs.json`:

```json
{
  "workers": 7,
  "jobs": [
    {
      "id": "extract",
      "type": "shell",
      "command": "echo extract",
      "category": "EOD",
      "priority": 5,
      "max_attempts": 3
    },
    {
      "id": "load",
      "type": "shell",
      "command": "echo load",
      "category": "EOD",
      "depends_on": ["extract"]
    }
  ]
}
```

Built-in CPU pricing example:

```json
{
  "jobs": [
    {
      "id": "cpu-price",
      "type": "cpu_pricing",
      "category": "EOD",
      "params": {
        "face_value": 100.0,
        "coupon_rate": 0.05,
        "maturity_years": 5,
        "yield": 0.05,
        "settlement_date": "2024-01-02"
      }
    }
  ]
}
```

Optional QuantLib bond pricing example:

```json
{
  "jobs": [
    {
      "id": "bond-price",
      "type": "quantlib_bond",
      "category": "EOD",
      "params": {
        "face_value": 100.0,
        "coupon_rate": 0.05,
        "maturity_years": 5,
        "yield": 0.05,
        "issue_date": "2023-10-02",
        "settlement_date": "2024-01-02"
      }
    }
  ]
}
```

`issue_date` is optional, must be strictly before `settlement_date`, and defaults to three months before it. QuantLib's evaluation date is process-global, so QuantLib pricing is serialized with a mutex.

## Exit codes

- `0`: all jobs succeeded
- `1`: one or more jobs failed
- `2`: invalid input, missing database, or invalid command-line option

## History commands

```sh
./run.sh history my.db --limit 5
./run.sh history my.db --job extract
./run.sh history my.db --job extract --limit 20
```

History is written only after the complete run finishes. A crash during a run loses that run; there is no crash recovery.

## Limitations

- SQLite history is saved only after a run finishes. A crash mid-run loses that run; there is no crash recovery.
- Retries are immediate; there is no backoff delay.
- There are no cron or calendar triggers.
- There is no REST API.
- Execution is limited to a single process.
- Shell jobs have no timeout.
- QuantLib support is optional; without it, the core and CPU-pricing job remain available.
