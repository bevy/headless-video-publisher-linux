#! /usr/bin/env bash

# VIDEO_FILE and AUDIO_FILE are defined by the Dockerfile to avoid duplication here.

if [ ! -f "$VIDEO_FILE" ]; then
  echo Missing video file "${VIDEO_FILE}"
  exit 1
fi

if [ ! -f "$AUDIO_FILE" ]; then
  echo Missing audio file "${AUDIO_FILE}"
  exit 1
fi

if [ "x$1" = "x-h" -o $# -lt 3 ]; then
  echo Usage: $0 apiKey sessionId token
  exit 0
fi

APIKEY="$1"
shift

SESSIONID="$1"
shift

TOKEN="$1"
shift

cmd=(./src/build/headless-video-publisher -k "$APIKEY" -s "$SESSIONID" -t "$TOKEN")
if [ "x$1" = "x-S" ]; then
    cmd+=("$1")
    shift
fi
if [ "x$1" != "x-J" ]; then
    cmd+=(-v "${VIDEO_FILE}" -a "${AUDIO_FILE}")
fi

"${cmd[@]}"
