#!/bin/sh
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
DRIVER_ROOT="$REPOSITORY_ROOT/apps/macos/Driver"
TOOLS_ROOT="$DRIVER_ROOT/build/Tools"
DEVICE_UID="ch.oki.cuelet.virtual-microphone"
EXPECTED_VERSION="0.1.11"
EXPECTED_BUILD="12"
EXPECTED_DIAGNOSTIC_HASH="9cad2be160bc79de737b71aa4cd8a0e94b8ac241193322a3d7c4a5dcd2a839c8"

MODE=live
if [ "${1:-}" = "--dry-run" ]; then
    MODE=dry-run
    shift
elif [ "${1:-}" = "--dry-run-event-failure" ]; then
    MODE=dry-run-event-failure
    shift
fi

DEFAULT_PREFIX="cuelet-driver-0111-live-diagnostics"
if [ "$MODE" != "live" ]; then
    DEFAULT_PREFIX="cuelet-driver-017-workflow-${MODE}"
fi
OUTPUT_ROOT=${1:-"/tmp/${DEFAULT_PREFIX}-$(date +%Y%m%d-%H%M%S)"}

INSPECTOR="$TOOLS_ROOT/cuelet-driver-diagnostics"
INJECTOR="$TOOLS_ROOT/cuelet-hal-injector"
RECEIVER="$TOOLS_ROOT/cuelet-hal-receiver"
ANALYZER="$DRIVER_ROOT/tools/analyze-cuelet-capture.py"
CANDIDATE_BUNDLE="$DRIVER_ROOT/build/Release/CueletVirtualAudio.driver"
CANDIDATE_EXECUTABLE="$CANDIDATE_BUNDLE/Contents/MacOS/CueletVirtualAudio"
INSTALLED_BUNDLE="/Library/Audio/Plug-Ins/HAL/CueletVirtualAudio.driver"
INSTALLED_EXECUTABLE="$INSTALLED_BUNDLE/Contents/MacOS/CueletVirtualAudio"

mkdir -p "$OUTPUT_ROOT/audio" "$OUTPUT_ROOT/logs"

overall_status=0
failure_count=0
current_stage=initial-status
watch_pid=
event_watch_pid=
receiver_pid=
installed_version=unknown
installed_build=unknown
installed_hash=unknown
candidate_hash=unknown

cleanup() {
    if [ -n "$receiver_pid" ] && kill -0 "$receiver_pid" 2>/dev/null; then
        kill -TERM "$receiver_pid" 2>/dev/null || true
    fi
    if [ -n "$watch_pid" ] && kill -0 "$watch_pid" 2>/dev/null; then
        kill -TERM "$watch_pid" 2>/dev/null || true
    fi
    if [ -n "$event_watch_pid" ] && kill -0 "$event_watch_pid" 2>/dev/null; then
        kill -TERM "$event_watch_pid" 2>/dev/null || true
    fi
}

initialize_summaries() {
    {
        echo "Cuelet live diagnostic workflow"
        echo "status=IN_PROGRESS"
        echo "validation_root=$OUTPUT_ROOT"
        echo "partial_evidence_retained=yes"
    } >"$OUTPUT_ROOT/logs/workflow-error-summary.txt"
    {
        echo "Cuelet 0.1.11 diagnostic run"
        echo "status=IN_PROGRESS"
        echo "validation_root=$OUTPUT_ROOT"
    } >"$OUTPUT_ROOT/logs/diagnosis-summary.txt"
}

record_failure() {
    failure_stage=$1
    failure_status=$2
    failure_detail=$3
    if [ "$failure_status" -eq 0 ]; then
        failure_status=1
    fi
    if [ "$overall_status" -eq 0 ]; then
        overall_status=$failure_status
    fi
    failure_count=$((failure_count + 1))
    {
        echo "failure_${failure_count}_stage=$failure_stage"
        echo "failure_${failure_count}_status=$failure_status"
        echo "failure_${failure_count}_detail=$failure_detail"
    } >>"$OUTPUT_ROOT/logs/workflow-error-summary.txt"
    echo "Cuelet diagnostic warning at $failure_stage: $failure_detail" >&2
}

run_logged() {
    run_stage=$1
    run_stdout=$2
    run_stderr=$3
    shift 3
    current_stage=$run_stage
    "$@" >"$run_stdout" 2>"$run_stderr"
    run_status=$?
    printf '%s\n' "$run_status" >"$run_stdout.exit-code"
    return "$run_status"
}

finish_summaries() {
    summary_status=PASS
    if [ "$overall_status" -ne 0 ]; then
        summary_status=PARTIAL_FAILURE
    fi
    {
        echo "status=$summary_status"
        echo "failure_count=$failure_count"
        echo "final_stage=$current_stage"
        echo "installed_version=$installed_version"
        echo "installed_build=$installed_build"
        echo "installed_sha256=$installed_hash"
        echo "candidate_sha256=$candidate_hash"
        echo "validation_root=$OUTPUT_ROOT"
        echo "partial_evidence_retained=yes"
    } >>"$OUTPUT_ROOT/logs/workflow-error-summary.txt"

    {
        echo "Cuelet 0.1.11 diagnostic run"
        echo "status=$summary_status"
        echo "validation_root=$OUTPUT_ROOT"
        echo "installed_version=$installed_version"
        echo "installed_build=$installed_build"
        echo "installed_sha256=$installed_hash"
        echo "candidate_sha256=$candidate_hash"
        echo "event_level_diagnosis=$([ -f "$OUTPUT_ROOT/logs/event-watch.stdout.exit-code" ] && [ "$(cat "$OUTPUT_ROOT/logs/event-watch.stdout.exit-code")" -eq 0 ] && echo complete || echo incomplete)"
        echo
        echo "Driver telemetry summary:"
        if [ -s "$OUTPUT_ROOT/logs/driver-summary.txt" ]; then
            cat "$OUTPUT_ROOT/logs/driver-summary.txt"
        else
            echo "unavailable"
        fi
        echo
        echo "Final counters:"
        if [ -s "$OUTPUT_ROOT/logs/final-status.jsonl" ]; then
            cat "$OUTPUT_ROOT/logs/final-status.jsonl"
        else
            echo "unavailable"
        fi
        echo
        echo "Receiver payload summary:"
        if [ -s "$OUTPUT_ROOT/logs/receiver-analysis.json" ]; then
            cat "$OUTPUT_ROOT/logs/receiver-analysis.json"
        else
            echo "unavailable"
        fi
        echo
        echo "Workflow failures:"
        if [ "$failure_count" -eq 0 ]; then
            echo "none"
        else
            cat "$OUTPUT_ROOT/logs/workflow-error-summary.txt"
        fi
    } >"$OUTPUT_ROOT/logs/diagnosis-summary.txt"
}

finish_and_exit() {
    cleanup
    finish_summaries
    trap - INT TERM HUP
    echo "$OUTPUT_ROOT"
    echo "Summary: $OUTPUT_ROOT/logs/diagnosis-summary.txt"
    if [ "$overall_status" -ne 0 ]; then
        echo "Partial evidence: $OUTPUT_ROOT/logs/workflow-error-summary.txt" >&2
    fi
    exit "$overall_status"
}

handle_signal() {
    record_failure "$current_stage" 130 "workflow interrupted; clients were stopped"
    finish_and_exit
}

initialize_summaries
trap handle_signal INT TERM HUP

for executable in "$INSPECTOR" "$INJECTOR" "$RECEIVER" "$ANALYZER"; do
    if [ ! -x "$executable" ]; then
        record_failure initial-status 1 \
            "missing executable: $executable; run make -C '$DRIVER_ROOT' tools"
        finish_and_exit
    fi
done

if [ ! -x "$CANDIDATE_EXECUTABLE" ]; then
    record_failure initial-status 1 \
        "missing Release candidate: $CANDIDATE_EXECUTABLE"
    finish_and_exit
fi
candidate_hash=$(shasum -a 256 "$CANDIDATE_EXECUTABLE" | awk '{print $1}')

if [ "$MODE" != "live" ]; then
    if ! run_logged initial-status \
        "$OUTPUT_ROOT/logs/candidate-verification.stdout" \
        "$OUTPUT_ROOT/logs/candidate-verification.stderr" \
        "$SCRIPT_DIR/verify-virtual-audio-driver.sh" "$CANDIDATE_BUNDLE"; then
        status=$(cat "$OUTPUT_ROOT/logs/candidate-verification.stdout.exit-code")
        record_failure initial-status "$status" "candidate verification failed"
    fi
    if ! run_logged initial-status \
        "$OUTPUT_ROOT/logs/inspector-selftest.stdout" \
        "$OUTPUT_ROOT/logs/inspector-selftest.stderr" \
        "$INSPECTOR" selftest; then
        status=$(cat "$OUTPUT_ROOT/logs/inspector-selftest.stdout.exit-code")
        record_failure initial-status "$status" "inspector decoder selftest failed"
    fi
    if [ "$MODE" = "dry-run-event-failure" ]; then
        current_stage=event-watch-finish
        echo "simulated cqev failure" >"$OUTPUT_ROOT/logs/event-watch.stderr"
        echo 72 >"$OUTPUT_ROOT/logs/event-watch.stdout.exit-code"
        record_failure event-watch-finish 72 \
            "simulated optional event stream failure; counters and summary retained"
        echo '{"type":"counters","dryRun":true}' \
            >"$OUTPUT_ROOT/logs/final-status.jsonl"
        echo "event_stream=unavailable counter_fallback=available" \
            >"$OUTPUT_ROOT/logs/driver-summary.txt"
        echo '{"dryRun":true,"captureAnalysis":"retained"}' \
            >"$OUTPUT_ROOT/logs/receiver-analysis.json"
    else
        echo 0 >"$OUTPUT_ROOT/logs/event-watch.stdout.exit-code"
        echo '{"type":"counters","dryRun":true}' \
            >"$OUTPUT_ROOT/logs/final-status.jsonl"
        echo "event_stream=ready counter_fallback=ready" \
            >"$OUTPUT_ROOT/logs/driver-summary.txt"
        echo '{"dryRun":true}' >"$OUTPUT_ROOT/logs/receiver-analysis.json"
    fi
    finish_and_exit
fi

if ! run_logged initial-status \
    "$OUTPUT_ROOT/logs/installed-verification.stdout" \
    "$OUTPUT_ROOT/logs/installed-verification.stderr" \
    env CUELET_EXPECT_DRIVER_DIAGNOSTICS=1 \
    "$SCRIPT_DIR/verify-virtual-audio-driver.sh" "$INSTALLED_BUNDLE"; then
    status=$(cat "$OUTPUT_ROOT/logs/installed-verification.stdout.exit-code")
    record_failure initial-status "$status" "installed identity verification failed"
    finish_and_exit
fi

installed_version=$(/usr/libexec/PlistBuddy -c \
    'Print :CFBundleShortVersionString' "$INSTALLED_BUNDLE/Contents/Info.plist" \
    2>"$OUTPUT_ROOT/logs/installed-version.stderr")
version_status=$?
installed_build=$(/usr/libexec/PlistBuddy -c \
    'Print :CFBundleVersion' "$INSTALLED_BUNDLE/Contents/Info.plist" \
    2>"$OUTPUT_ROOT/logs/installed-build.stderr")
build_status=$?
installed_hash=$(shasum -a 256 "$INSTALLED_EXECUTABLE" 2>/dev/null | awk '{print $1}')
hash_status=$?
if [ "$version_status" -ne 0 ] || [ "$build_status" -ne 0 ] || \
   [ "$hash_status" -ne 0 ]; then
    record_failure initial-status 1 "could not read installed driver identity"
    finish_and_exit
fi
if [ "$installed_version" != "$EXPECTED_VERSION" ] || \
   [ "$installed_build" != "$EXPECTED_BUILD" ]; then
    record_failure initial-status 1 \
        "expected $EXPECTED_VERSION build $EXPECTED_BUILD; found $installed_version build $installed_build"
    finish_and_exit
fi
if [ "$installed_hash" != "$EXPECTED_DIAGNOSTIC_HASH" ]; then
    record_failure initial-status 1 \
        "installed hash does not match the validated 0.1.11 diagnostic build"
    finish_and_exit
fi

if ! run_logged initial-status \
    "$OUTPUT_ROOT/logs/status-before.jsonl" \
    "$OUTPUT_ROOT/logs/status-before.stderr" \
    "$INSPECTOR" status; then
    status=$(cat "$OUTPUT_ROOT/logs/status-before.jsonl.exit-code")
    record_failure initial-status "$status" "required counter properties unavailable"
    finish_and_exit
fi
if ! run_logged clear \
    "$OUTPUT_ROOT/logs/clear.jsonl" \
    "$OUTPUT_ROOT/logs/clear.stderr" \
    "$INSPECTOR" clear; then
    status=$(cat "$OUTPUT_ROOT/logs/clear.jsonl.exit-code")
    record_failure clear "$status" "diagnostic clear failed"
    finish_and_exit
fi
if ! run_logged initial-status \
    "$OUTPUT_ROOT/logs/baseline.jsonl" \
    "$OUTPUT_ROOT/logs/baseline.stderr" \
    "$INSPECTOR" status; then
    status=$(cat "$OUTPUT_ROOT/logs/baseline.jsonl.exit-code")
    record_failure initial-status "$status" "baseline counter snapshot failed"
    finish_and_exit
fi

current_stage=event-watch-start
"$INSPECTOR" watch-events 15 50 \
    "$OUTPUT_ROOT/logs/driver-events-stream.jsonl" \
    >"$OUTPUT_ROOT/logs/event-watch.stdout" \
    2>"$OUTPUT_ROOT/logs/event-watch.stderr" &
event_watch_pid=$!

current_stage=counter-watch-start
"$INSPECTOR" watch 15 100 "$OUTPUT_ROOT/logs/counter-watch.jsonl" \
    >"$OUTPUT_ROOT/logs/counter-watch.stdout" \
    2>"$OUTPUT_ROOT/logs/counter-watch.stderr" &
watch_pid=$!

current_stage=receiver-start
"$RECEIVER" "$DEVICE_UID" 14 \
    "$OUTPUT_ROOT/audio/capture-48k.wav" \
    "$OUTPUT_ROOT/logs/receiver-events.jsonl" 48000 \
    >"$OUTPUT_ROOT/logs/receiver-summary.txt" \
    2>"$OUTPUT_ROOT/logs/receiver.stderr" &
receiver_pid=$!

# Establishes receiver-first ordering outside all real-time callbacks.
sleep 1

if ! run_logged injector-run \
    "$OUTPUT_ROOT/logs/injector-events.jsonl" \
    "$OUTPUT_ROOT/logs/injector.stderr" \
    "$INJECTOR" "$DEVICE_UID" 10 48000; then
    status=$(cat "$OUTPUT_ROOT/logs/injector-events.jsonl.exit-code")
    record_failure injector-run "$status" "injector failed"
fi

current_stage=receiver-finish
wait "$receiver_pid"
receiver_status=$?
receiver_pid=
printf '%s\n' "$receiver_status" \
    >"$OUTPUT_ROOT/logs/receiver-summary.txt.exit-code"
if [ "$receiver_status" -ne 0 ]; then
    record_failure receiver-finish "$receiver_status" "receiver failed"
fi

current_stage=event-watch-finish
wait "$event_watch_pid"
event_watch_status=$?
event_watch_pid=
printf '%s\n' "$event_watch_status" \
    >"$OUTPUT_ROOT/logs/event-watch.stdout.exit-code"
if [ "$event_watch_status" -ne 0 ]; then
    record_failure event-watch-finish "$event_watch_status" \
        "optional event watch failed; continuing with aggregate counters"
fi

current_stage=counter-watch-finish
wait "$watch_pid"
watch_status=$?
watch_pid=
printf '%s\n' "$watch_status" \
    >"$OUTPUT_ROOT/logs/counter-watch.stdout.exit-code"
if [ "$watch_status" -ne 0 ]; then
    record_failure counter-watch-finish "$watch_status" \
        "counter watch failed; final counter snapshot will still be attempted"
fi

if ! run_logged final-snapshot \
    "$OUTPUT_ROOT/logs/final-status.jsonl" \
    "$OUTPUT_ROOT/logs/final-status.stderr" \
    "$INSPECTOR" status; then
    status=$(cat "$OUTPUT_ROOT/logs/final-status.jsonl.exit-code")
    record_failure final-snapshot "$status" "final counter snapshot failed"
fi
if ! run_logged final-snapshot \
    "$OUTPUT_ROOT/logs/driver-summary.txt" \
    "$OUTPUT_ROOT/logs/driver-summary.stderr" \
    "$INSPECTOR" summarize; then
    status=$(cat "$OUTPUT_ROOT/logs/driver-summary.txt.exit-code")
    record_failure final-snapshot "$status" "counter summary failed"
fi
if ! run_logged final-snapshot \
    "$OUTPUT_ROOT/logs/snapshot.stdout" \
    "$OUTPUT_ROOT/logs/snapshot.stderr" \
    "$INSPECTOR" snapshot "$OUTPUT_ROOT/logs/driver-events-final.jsonl"; then
    status=$(cat "$OUTPUT_ROOT/logs/snapshot.stdout.exit-code")
    record_failure final-snapshot "$status" \
        "optional final event snapshot failed; aggregate evidence retained"
fi

if [ -s "$OUTPUT_ROOT/audio/capture-48k.wav" ]; then
    if ! run_logged capture-analysis \
        "$OUTPUT_ROOT/logs/receiver-analysis.stdout" \
        "$OUTPUT_ROOT/logs/receiver-analysis.stderr" \
        "$ANALYZER" "$OUTPUT_ROOT/audio/capture-48k.wav" \
            --output "$OUTPUT_ROOT/logs/receiver-analysis.json"; then
        status=$(cat "$OUTPUT_ROOT/logs/receiver-analysis.stdout.exit-code")
        record_failure capture-analysis "$status" "capture analysis failed"
    fi
else
    record_failure capture-analysis 1 "receiver WAV was not produced"
fi

current_stage=summary
finish_and_exit
