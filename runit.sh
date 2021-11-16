#! /usr/bin/env bash

VIDEO_FILE=source.yuv
AUDIO_FILE=source.pcm

if [ ! -f "$VIDEO_FILE" ]; then
  echo Missing video file "${VIDEO_FILE}"
  exit 1
fi

if [ ! -f "$AUDIO_FILE" ]; then
  echo Missing audio file "${AUDIO_FILE}"
  exit 1
fi

if [ "x$1" = "x-h" -o $# -ne 3 ]; then
  echo Usage: $0 apiKey sessionId token
  exit 0
fi

APIKEY="$1"
SESSIONID="$2"
TOKEN="$3"

./src/build/headless-video-publisher -v "${VIDEO_FILE}" -a "${AUDIO_FILE}" -k "$APIKEY" -s "$SESSIONID" -t "$TOKEN"
