#!/usr/bin/env bash
# Tags: no-parallel

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

$CLICKHOUSE_CLIENT --query="select toUInt64(pow(2, 62)) as value format JSON" --output_format_json_quote_64bit_integers=0 | grep value
$CLICKHOUSE_CLIENT --query="select toUInt64(pow(2, 62)) as value format JSON" --output_format_json_quote_64bit_integers=1 | grep value

$CLICKHOUSE_CLIENT --readonly=1 --query="set output_format_json_quote_64bit_integers=1 ; select toUInt64(pow(2, 63)) as value format JSON" --server_logs_file=/dev/null 2>&1 | grep -o -q 'value\|Cannot modify .* setting in readonly mode' && echo "OK" || echo "FAIL"
$CLICKHOUSE_CLIENT --readonly=1 --query="set output_format_json_quote_64bit_integers=0 ; select toUInt64(pow(2, 63)) as value format JSON" --server_logs_file=/dev/null 2>&1 | grep -o -q 'value\|Cannot modify .* setting in readonly mode' && echo "OK" || echo "FAIL"

${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}&query=SELECT+toUInt64(pow(2,+63))+as+value+format+JSON&output_format_json_quote_64bit_integers=1" | grep value
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}&query=SELECT+toUInt64(pow(2,+63))+as+value+format+JSON&output_format_json_quote_64bit_integers=0" | grep value

${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}&session_id=readonly&query=SELECT+toUInt64(pow(2,+63))+as+value+format+JSON&output_format_json_quote_64bit_integers=1" 2>&1 | grep -o -q 'value\|Cannot modify .* setting in readonly mode' && echo "OK" || echo "FAIL"
${CLICKHOUSE_CURL} -sS "${CLICKHOUSE_URL}&session_id=readonly&query=SELECT+toUInt64(pow(2,+63))+as+value+format+JSON&output_format_json_quote_64bit_integers=0" 2>&1 | grep -o -q 'value\|Cannot modify .* setting in readonly mode' && echo "OK" || echo "FAIL"
