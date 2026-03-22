FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libasio-dev \
    libbz2-dev \
    libexpat1-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend

COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY data/stopwords.txt ./data/stopwords.txt
COPY external/Crow ./external/Crow

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j"$(nproc)"

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libbz2-1.0 \
    libexpat1 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend/build

COPY --from=build /app/backend/build/wikilens /app/backend/build/wikilens
COPY --from=build /app/backend/data/stopwords.txt /app/backend/data/stopwords.txt

RUN mkdir -p /app/backend/data/cache /data/dumps

EXPOSE 8080

CMD ["./wikilens"]
