# NOTES:
#    This is lifted from the README.md file of the headless-video-publisher-linux repo.
#    In addition, to save space in the source repo (beacuse the yuv file esp is 350Mb for just the video)
#      we also install ffmpeg here.
#    Expected: `source.mp4` a 10-second(ish) long mp4 file to be processed.

FROM debian:buster

WORKDIR demo

COPY script.deb.sh ./
RUN ./script.deb.sh
RUN apt-get install -y libopentok-dev
RUN apt-get install -y build-essential cmake clang libc++-dev libc++abi-dev pkg-config libasound2 libpulse-dev libsdl2-dev

# Not strictly needed to run the client, but by processing the desired file inside here, we don't need to check in large files to source control.
RUN apt-get install -y ffmpeg

COPY common ./common/
COPY src ./src/

WORKDIR src/build
RUN CC=clang CXX=clang++ cmake ..
RUN make
WORKDIR ../..

COPY Dockerfile LICENSE README.md ./

# Since this is the most likely-to-change file, isolate it for better docker layer cache re-use:
COPY source.mp4 runit.sh ./

# Convert to 30 frames per second to allow for varying frame rate sources.
RUN ffmpeg -i source.mp4 -filter:v fps=30 source-at-30fps.mp4

# Now convert to raw YUV frames needed by the test code (this is where things get BIG)
RUN ffmpeg -i source-at-30fps.mp4 source.yuv

# Now, create a raw PCM audio clip from the same mp4 file
RUN ffmpeg -y  -i source-at-30fps.mp4 -acodec pcm_s16le -f s16le -ac 1 -ar 16000 source.pcm

# This is just a demo, so you can connect to it and play around with the built program and sample files.
ENTRYPOINT /bin/bash
