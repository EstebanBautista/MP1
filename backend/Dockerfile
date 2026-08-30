FROM japeto/so-tools:v64

WORKDIR /app

# Header-only dependencies vendored in-tree (websocketpp, asio standalone, nlohmann/json)
COPY third_party third_party
COPY src src
COPY server.cpp .

# -DASIO_STANDALONE makes websocketpp use the vendored standalone asio instead of Boost.
RUN g++ -std=c++14 -O2 -DASIO_STANDALONE \
    -I third_party/websocketpp \
    -I third_party/asio/include \
    -I third_party/nlohmann-json/single_include \
    server.cpp src/world.cpp src/protocol.cpp \
    -o server -pthread

EXPOSE 5000

CMD ["./server"]