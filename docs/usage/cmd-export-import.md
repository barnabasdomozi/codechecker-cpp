# Export and import comments and review statuses (`cmd export`, `cmd import`)

Copies review statuses and comments between servers or products, e.g. when
moving to a new server. Reports are matched by their hash.

## Export ✅

```sh
CodeChecker cmd export -n demo --url localhost:8555/Default > export.json
CodeChecker cmd export --url localhost:8555/Default > all.json    # every run
```

The JSON goes to standard output; log lines go to standard error.

```json
{"comments": {},
 "reviewData": {
   "21b0090745a8a3abdf9a0989d93a8ce7": {"status": 2, "comment": "intentional for the demo",
       "author": "Anonymous", "date": "2026-10-23 23:39:49.942365", "isInSource": true},
   "07a8f6647771d7458a6a8cf8e4b15643": {"status": 2, "comment": "demo comment",
       "author": "Anonymous", "date": "2026-10-23 23:39:49.942365", "isInSource": true}}}
```

Keys are report hashes. `status` uses the Thrift review status numbers.
`comments` holds comments by report hash. These include the system comments
the server adds when a review status changes, e.g.:

```json
"comments": {"07a8f6647771d7458a6a8cf8e4b15643": [
  {"id": 2, "author": "Anonymous",
   "message": "rev_st_changed_msg Unreviewed False_positive 'demo comment'", ...}]}
```

(seen in a later export of all runs).

## Import ✅

```sh
CodeChecker cmd import -i export.json --url localhost:8555/Default
```

```
[INFO] - Import successful!
```
