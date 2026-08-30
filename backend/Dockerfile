FROM japeto/so-tools:v64

WORKDIR /app

# Header-only dependencies vendored in-tree (asio standalone, nlohmann/json).
# websocketpp was removed on this branch: the client polls game state over HTTP.
COPY third_party third_party
COPY src src
COPY server.cpp .

# -DASIO_STANDALONE makes the vendored standalone asio provide the `asio::` API.
RUN g++ -std=c++14 -O2 -DASIO_STANDALONE \
    -I third_party/asio/include \
    -I third_party/nlohmann-json/single_include \
    server.cpp src/world.cpp src/protocol.cpp \
    -o server -pthread

EXPOSE 5000

CMD ["./server"]