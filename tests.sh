#!/bin/bash
# Test harness for blueberry test suite

PASS=0
FAIL=0
SKIP=0
ERRORS=""
SKIPPED=""

for f in tests/**/*.ci; do
	[ -f "$f" ] || continue
	# skip helpers
	case "$f" in */test_helpers.ci) continue;; esac
	name=$(basename "$f" .ci)
	output=$(./blueberry "$f" 2>&1)
	status=$?

	passed=$(echo "$output" | grep -c '<PASS')
	failed=$(echo "$output" | grep -c '<FAIL')
	skipped=$(echo "$output" | grep -c '<SKIP')

	if [ $status -ne 0 ] || [ $failed -gt 0 ]; then
		echo "FAIL  $name ($passed passed, $failed failed, $skipped skipped, exit=$status)"
		ERRORS="$ERRORS\n--- $name ---\n$(echo "$output" | grep '<FAIL')"
		FAIL=$((FAIL + 1))
	else
		if [ $skipped -gt 0 ]; then
			echo "OK    $name ($passed passed, $skipped skipped)"
		else
			echo "OK    $name ($passed passed)"
		fi
		PASS=$((PASS + 1))
	fi

	if [ $skipped -gt 0 ]; then
		SKIP=$((SKIP + skipped))
		SKIPPED="$SKIPPED\n--- $name ---\n$(echo "$output" | grep '<SKIP')"
	fi
done

echo ""
echo "==============================="
echo "Files: $((PASS + FAIL))  OK: $PASS  FAIL: $FAIL"
echo "Skipped tests: $SKIP"
echo "==============================="

if [ -n "$SKIPPED" ]; then
	echo ""
	echo "Skipped:"
	echo -e "$SKIPPED"
fi

if [ -n "$ERRORS" ]; then
	echo ""
	echo "Failures:"
	echo -e "$ERRORS"
	exit 1
fi
