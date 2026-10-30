# Build stage: compiles the application with the CI flags and runs the full test suite.
FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make git ca-certificates libssl-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" \
 && cmake --build build --parallel \
 && ctest --test-dir build --output-on-failure --no-tests=error

# Runtime stage: the application binary only, run as an unprivileged user.
FROM ubuntu:24.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends libssl3t64 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --create-home rrs
COPY --from=build /src/build/railway_reservation /usr/local/bin/railway_reservation
USER rrs
WORKDIR /home/rrs
ENTRYPOINT ["railway_reservation"]
