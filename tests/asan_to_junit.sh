#!/bin/bash
# Convert AddressSanitizer plain-text log files to JUnit XML.
#
# Usage: asan_to_junit.sh [FILE ...]
# Output: JUnit XML on stdout.
#
# Each ASan log file (produced via ASAN_OPTIONS="log_path=...") is parsed into
# a <testcase> with a <failure> element containing the full error report.
# If no files are given or all files are empty, a single passing test case
# ("asan-clean") is emitted.

set -euo pipefail

errors=()
current_error=""
current_type=""
current_message=""

flush_error() {
  if [ -n "$current_error" ]; then
    errors+=("$(printf '%s\n' "$current_type" "$current_message" "$current_error")")
  fi
  current_error=""
  current_type=""
  current_message=""
}

# XML-escape special characters
xml_escape() {
  sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g; s/"/\&quot;/g; s/'"'"'/\&apos;/g'
}

# Collect all non-empty files from arguments
files=()
for f in "$@"; do
  [ -f "$f" ] && [ -s "$f" ] && files+=("$f")
done

# Parse each log file for ASan error reports
for f in "${files[@]}"; do
  in_error=0
  while IFS= read -r line; do
    # Detect the ERROR: header line
    if [[ "$line" =~ ==([0-9]+)==ERROR:\ AddressSanitizer:\ (.+) ]]; then
      flush_error
      in_error=1
      current_type="${BASH_REMATCH[2]%%on *}"
      current_type="${current_type%% }"
      current_message="$line"
      current_error="$line"
      continue
    fi
    # Detect the SUMMARY line (marks end of a single report)
    if [[ "$line" =~ ^SUMMARY:\ AddressSanitizer ]]; then
      current_error="${current_error}"$'\n'"${line}"
      flush_error
      in_error=0
      continue
    fi
    if [ "$in_error" -eq 1 ]; then
      current_error="${current_error}"$'\n'"${line}"
    fi
  done < "$f"
  flush_error
done

count=${#errors[@]}

# Emit JUnit XML
echo '<?xml version="1.0" encoding="UTF-8"?>'
echo '<testsuites>'

if [ "$count" -eq 0 ]; then
  echo '  <testsuite name="AddressSanitizer" tests="1" failures="0" errors="0">'
  echo '    <testcase name="asan-clean" classname="AddressSanitizer" time="0"/>'
else
  echo "  <testsuite name=\"AddressSanitizer\" tests=\"$count\" failures=\"$count\" errors=\"0\">"
  i=0
  for entry in "${errors[@]}"; do
    i=$((i + 1))
    type=$(echo "$entry" | head -1)
    message=$(echo "$entry" | sed -n '2p')
    body=$(echo "$entry" | tail -n +3)

    escaped_type=$(printf '%s' "$type" | xml_escape)
    escaped_message=$(printf '%s' "$message" | xml_escape)
    escaped_body=$(printf '%s' "$body" | xml_escape)

    echo "    <testcase name=\"${escaped_type}-${i}\" classname=\"AddressSanitizer\" time=\"0\">"
    echo "      <failure type=\"${escaped_type}\" message=\"${escaped_message}\">"
    echo "$escaped_body"
    echo '      </failure>'
    echo '    </testcase>'
  done
fi

echo '  </testsuite>'
echo '</testsuites>'
