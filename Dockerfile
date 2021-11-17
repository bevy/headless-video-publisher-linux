# NOTES:
#    This is lifted from the README.md file of the headless-video-publisher-linux repo.
#    In addition, to save space in the source repo (beacuse the yuv file esp is 350Mb for just the video)
#      we also install ffmpeg here.
#    Expected: `source.mp4` a 10-second(ish) long mp4 file to be processed, in the root of this repo.

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

ENV MP4_FILE=source.mp4 VIDEO_FILE=source.yuv AUDIO_FILE=source.pcm

# Since this is the most likely-to-change file, isolate it for better docker layer cache re-use:
COPY $MP4_FILE runit.sh ./

# Convert to 30 frames per second to allow for varying frame rate sources.
RUN ffmpeg -i $MP4_FILE -filter:v fps=30 30fps_$MP4_FILE

# Now convert to raw YUV frames needed by the test code (this is where things get BIG)
RUN ffmpeg -i 30fps_$MP4_FILE $VIDEO_FILE

# Now, create a raw PCM audio clip from the same mp4 file
RUN ffmpeg -y  -i 30fps_$MP4_FILE -acodec pcm_s16le -f s16le -ac 1 -ar 16000 $AUDIO_FILE

# Pass needed parameters to the helper script as part of the docker run command,
# as they are too dynamic to default anything here.
ENTRYPOINT ["./runit.sh"]
